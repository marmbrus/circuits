"""Decodes the mic audio boards publish on sensor/<mac>/audio/mic.

Each MQTT message is one block, as built by the firmware's mic_codec.c (whose header describes
the layout): a 14-byte header holding the sample rate, a sequence number and the IMA-ADPCM
encoder's state, then 4-bit codes, the earlier sample in the low nibble. Blocks decode on their
own, so a dropped one only loses its own audio; a gap in sequence numbers says how many.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass

BLOCK_VERSION = 1
CODEC_IMA_ADPCM = 1
HEADER = struct.Struct("<BBHIHhBB")

_STEPS = (
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60,
    66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371,
    408, 449, 494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707,
    1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484,
    7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385,
    24623, 27086, 29794, 32767,
)
_INDEX = (-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8)


@dataclass
class MicBlock:
    sequence: int
    sample_rate: int
    samples: list[int]


def decode_block(payload: bytes) -> MicBlock:
    """Decodes one message into 16-bit samples. Raises ValueError on anything malformed."""
    if len(payload) < HEADER.size:
        raise ValueError(f"block too short ({len(payload)} bytes)")
    version, codec, rate, seq, count, predictor, index, _ = HEADER.unpack_from(payload)
    if version != BLOCK_VERSION or codec != CODEC_IMA_ADPCM:
        raise ValueError(f"unknown block version {version} or codec {codec}")
    if index > 88:
        raise ValueError(f"bad step index {index}")
    codes = payload[HEADER.size:]
    if len(codes) < (count + 1) // 2:
        raise ValueError(f"block holds {len(codes)} bytes of codes for {count} samples")

    samples = []
    for i in range(count):
        code = codes[i // 2] >> 4 if i % 2 else codes[i // 2] & 15
        step = _STEPS[index]
        diff = step >> 3
        if code & 4:
            diff += step
        if code & 2:
            diff += step >> 1
        if code & 1:
            diff += step >> 2
        predictor += -diff if code & 8 else diff
        predictor = max(-32768, min(32767, predictor))
        index = max(0, min(88, index + _INDEX[code]))
        samples.append(predictor)
    return MicBlock(seq, rate, samples)
