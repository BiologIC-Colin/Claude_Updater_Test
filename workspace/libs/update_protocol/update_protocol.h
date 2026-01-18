/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef UPDATE_PROTOCOL_H_
#define UPDATE_PROTOCOL_H_

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define UPDATE_PROTOCOL_VERSION 1
#define UPDATE_PROTOCOL_MAX_PAYLOAD 64

/**
 * @brief Calculate CRC32 for firmware image
 *
 * @param data Pointer to data
 * @param len Length of data
 * @return CRC32 value
 */
uint32_t update_protocol_crc32(const uint8_t *data, size_t len);

/**
 * @brief Encode start message
 *
 * @param buffer Output buffer
 * @param buf_len Buffer length
 * @param image_size Total image size
 * @return Number of bytes written, or negative error code
 */
int update_protocol_encode_start(uint8_t *buffer, size_t buf_len, uint32_t image_size);

/**
 * @brief Encode data message
 *
 * @param buffer Output buffer
 * @param buf_len Buffer length
 * @param sequence Sequence number
 * @param data Data payload
 * @param data_len Data length
 * @return Number of bytes written, or negative error code
 */
int update_protocol_encode_data(uint8_t *buffer, size_t buf_len,
                                 uint16_t sequence, const uint8_t *data,
                                 size_t data_len);

/**
 * @brief Encode end message
 *
 * @param buffer Output buffer
 * @param buf_len Buffer length
 * @param crc32 Image CRC32
 * @return Number of bytes written, or negative error code
 */
int update_protocol_encode_end(uint8_t *buffer, size_t buf_len, uint32_t crc32);

#ifdef __cplusplus
}
#endif

#endif /* UPDATE_PROTOCOL_H_ */
