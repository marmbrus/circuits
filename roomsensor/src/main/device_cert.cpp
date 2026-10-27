#include "device_cert.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "nvs.h"
#include "mbedtls/oid.h"
#include "mbedtls/pk.h"
#include "mbedtls/x509_crt.h"

static const char* TAG = "device_cert";
static const char* NVS_NAMESPACE = "device_cert";

// Read at boot and kept for the life of the program: the MQTT and HTTP clients keep pointers to
// them. All three or none.
static char* s_cert = nullptr;
static char* s_key = nullptr;
static char* s_ca = nullptr;

// The PEMs are stored without their terminating NUL; this adds it back. nullptr when missing.
static char* read_pem(nvs_handle_t nvs, const char* key) {
    size_t len = 0;
    if (nvs_get_blob(nvs, key, nullptr, &len) != ESP_OK || len == 0) {
        return nullptr;
    }
    char* pem = (char*)malloc(len + 1);
    if (pem == nullptr) {
        return nullptr;
    }
    if (nvs_get_blob(nvs, key, pem, &len) != ESP_OK) {
        free(pem);
        return nullptr;
    }
    pem[len] = '\0';
    return pem;
}

static int fill_random(void* ctx, unsigned char* buf, size_t len) {
    (void)ctx;
    esp_fill_random(buf, len);
    return 0;
}

static bool common_name_is(const mbedtls_x509_crt* cert, const char* expected) {
    for (const mbedtls_x509_name* name = &cert->subject; name != nullptr; name = name->next) {
        if (MBEDTLS_OID_CMP(MBEDTLS_OID_AT_CN, &name->oid) == 0) {
            return name->val.len == strlen(expected) && memcmp(name->val.p, expected, name->val.len) == 0;
        }
    }
    return false;
}

void device_cert_init(void) {
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) == ESP_OK) {
        s_cert = read_pem(nvs, "cert");
        s_key = read_pem(nvs, "key");
        s_ca = read_pem(nvs, "ca");
        nvs_close(nvs);
    }
    char name[24];
    device_cert_common_name(name, sizeof(name));
    if (s_cert != nullptr && s_key != nullptr && s_ca != nullptr) {
        ESP_LOGI(TAG, "Have a certificate for %s", name);
        return;
    }
    ESP_LOGW(TAG, "No certificate for %s: updates.acgt.net will refuse OTA, and an mqtts:// broker the connection", name);
    free(s_cert);
    free(s_key);
    free(s_ca);
    s_cert = s_key = s_ca = nullptr;
}

void device_cert_common_name(char* out, size_t len) {
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(out, len, "ESP_%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

void device_cert_configure_mqtt(esp_mqtt_client_config_t* cfg) {
    const char* uri = cfg->broker.address.uri;
    if (uri == nullptr || strncmp(uri, "mqtts://", 8) != 0) {
        if (s_cert != nullptr) {
            ESP_LOGI(TAG, "The broker isn't mqtts://; connecting without the certificate");
        }
        return;
    }
    if (s_cert == nullptr) {
        ESP_LOGW(TAG, "The broker is mqtts:// but there's no certificate; it will refuse the connection");
        return;
    }
    cfg->broker.verification.certificate = s_ca;
    cfg->credentials.authentication.certificate = s_cert;
    cfg->credentials.authentication.key = s_key;
    ESP_LOGI(TAG, "Connecting to the broker with the certificate");
}

void device_cert_configure_http(esp_http_client_config_t* cfg) {
    if (s_cert == nullptr) {
        return;
    }
    cfg->client_cert_pem = s_cert;
    cfg->client_key_pem = s_key;
}

esp_err_t device_cert_store(const char* cert_pem, const char* key_pem, const char* ca_pem) {
    char name[24];
    device_cert_common_name(name, sizeof(name));

    mbedtls_x509_crt cert;
    mbedtls_x509_crt ca;
    mbedtls_pk_context key;
    mbedtls_x509_crt_init(&cert);
    mbedtls_x509_crt_init(&ca);
    mbedtls_pk_init(&key);

    esp_err_t err = ESP_ERR_INVALID_ARG;
    int ret;
    // A PEM's length for mbedtls includes its NUL.
    if ((ret = mbedtls_x509_crt_parse(&cert, (const unsigned char*)cert_pem, strlen(cert_pem) + 1)) != 0) {
        ESP_LOGE(TAG, "The certificate doesn't parse: -0x%04x", -ret);
    } else if ((ret = mbedtls_pk_parse_key(&key, (const unsigned char*)key_pem, strlen(key_pem) + 1, nullptr, 0, fill_random, nullptr)) != 0) {
        ESP_LOGE(TAG, "The key doesn't parse: -0x%04x", -ret);
    } else if ((ret = mbedtls_x509_crt_parse(&ca, (const unsigned char*)ca_pem, strlen(ca_pem) + 1)) != 0) {
        ESP_LOGE(TAG, "The authorities don't parse: %d", ret);
    } else if (mbedtls_pk_check_pair(&cert.pk, &key, fill_random, nullptr) != 0) {
        ESP_LOGE(TAG, "The key doesn't belong to the certificate");
    } else if (!common_name_is(&cert, name)) {
        ESP_LOGE(TAG, "The certificate isn't for this board, which is %s", name);
    } else {
        nvs_handle_t nvs;
        err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
        if (err == ESP_OK) {
            if ((err = nvs_set_blob(nvs, "cert", cert_pem, strlen(cert_pem))) == ESP_OK &&
                (err = nvs_set_blob(nvs, "key", key_pem, strlen(key_pem))) == ESP_OK &&
                (err = nvs_set_blob(nvs, "ca", ca_pem, strlen(ca_pem))) == ESP_OK) {
                err = nvs_commit(nvs);
            }
            nvs_close(nvs);
        }
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Can't store the certificate: %s", esp_err_to_name(err));
        }
    }

    mbedtls_pk_free(&key);
    mbedtls_x509_crt_free(&ca);
    mbedtls_x509_crt_free(&cert);
    return err;
}

esp_err_t device_cert_erase(void) {
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_erase_all(nvs);
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    return err;
}

void device_cert_print_status(void) {
    char name[24];
    device_cert_common_name(name, sizeof(name));
    printf("DEVICE CERT name: %s\n", name);

    char* pem = nullptr;
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) == ESP_OK) {
        pem = read_pem(nvs, "cert");
        nvs_close(nvs);
    }
    if (pem == nullptr) {
        printf("DEVICE CERT certificate: none\n");
        return;
    }

    mbedtls_x509_crt cert;
    mbedtls_x509_crt_init(&cert);
    if (mbedtls_x509_crt_parse(&cert, (const unsigned char*)pem, strlen(pem) + 1) == 0) {
        char dn[160];
        mbedtls_x509_dn_gets(dn, sizeof(dn), &cert.subject);
        printf("DEVICE CERT certificate: %s\n", dn);
        mbedtls_x509_dn_gets(dn, sizeof(dn), &cert.issuer);
        printf("DEVICE CERT issuer: %s\n", dn);
        printf("DEVICE CERT expires: %04d-%02d-%02d\n", cert.valid_to.year, cert.valid_to.mon, cert.valid_to.day);
    } else {
        printf("DEVICE CERT certificate: unreadable\n");
    }
    mbedtls_x509_crt_free(&cert);
    free(pem);
}
