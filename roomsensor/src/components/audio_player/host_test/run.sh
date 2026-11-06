#!/usr/bin/env bash
# Builds and runs mic_codec's host tests with the system C compiler, then checks that the Python
# decoder listeners use (util/roomsensor_util/mic.py) decodes the same blocks to the same samples.
set -euo pipefail
cd "$(dirname "$0")"
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT
${CC:-gcc} -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -D_DEFAULT_SOURCE -I.. \
    ../mic_codec.c test_mic_codec.c -lm -o "$OUT/test_mic_codec"
"$OUT/test_mic_codec" "$OUT/blocks.bin" "$OUT/decoded.bin"

python3 - "$OUT/blocks.bin" "$OUT/decoded.bin" "$(cd ../../../util && pwd)" <<'EOF'
import struct, sys
sys.path.insert(0, sys.argv[3])
from roomsensor_util.mic import decode_block

blocks = open(sys.argv[1], "rb").read()
expected = open(sys.argv[2], "rb").read()
samples, seqs, pos = [], [], 0
while pos < len(blocks):
    (n,) = struct.unpack_from("<I", blocks, pos)
    block = decode_block(blocks[pos + 4:pos + 4 + n])
    samples += block.samples
    seqs.append(block.sequence)
    pos += 4 + n
want = list(struct.unpack(f"<{len(expected) // 2}h", expected))
assert seqs == list(range(len(seqs))), seqs
assert samples == want, "Python decoder disagrees with the C decoder"
print(f"mic.py: decodes all {len(seqs)} blocks exactly as the firmware's codec does")
EOF
