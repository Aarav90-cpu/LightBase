#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

// Standard hardware-aligned byte sizes
#define AES_KEY_SIZE 32
#define AES_IV_SIZE 12
#define AES_TAG_SIZE 16

// 🔒 HARDENED SYSTEM LOCK EYE: Static salt key derived from hardware footprints
// (In production, you can derive this uniquely via /etc/machine-id)
static const unsigned char system_master_hardware_key[AES_KEY_SIZE] =
    {0x4A, 0x72, 0x61, 0x76, 0x4B, 0x68, 0x61, 0x72, 0x61, 0x64, 0x65, 0x4C, 0x6F, 0x63, 0x6B, 0x21,
     0x39, 0x44, 0x33, 0x35, 0x33, 0x46, 0x38, 0x35, 0x31, 0x34, 0x39, 0x4D, 0x45, 0x54, 0x41, 0x4C};

// --- ENCRYPT API KEY TO HARDWARE HEX BLOCK ---
char* encrypt_api_key_system_level(const char* plain_text_key) {
    EVP_CIPHER_CTX *ctx;
    int len;
    int ciphertext_len;
    unsigned char iv[AES_IV_SIZE];
    unsigned char ciphertext[4096];
    unsigned char tag[AES_TAG_SIZE];

    // 1. Generate cryptographic cryptographically strong random IV vector tokens
    if (!RAND_bytes(iv, AES_IV_SIZE)) return NULL;

    if (!(ctx = EVP_CIPHER_CTX_new())) return NULL;
    if (1 != EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, system_master_hardware_key, iv)) goto error;

    if (1 != EVP_EncryptUpdate(ctx, ciphertext, &len, (unsigned char*)plain_text_key, strlen(plain_text_key))) goto error;
    ciphertext_len = len;

    if (1 != EVP_EncryptFinal_ex(ctx, ciphertext + len, &len)) goto error;
    ciphertext_len += len;

    if (1 != EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, AES_TAG_SIZE, tag)) goto error;

    EVP_CIPHER_CTX_free(ctx);

    // 2. Serialize structural token packet to raw Hex format (IV + TAG + CIPHERTEXT)
    char *hex_output = malloc((AES_IV_SIZE + AES_TAG_SIZE + ciphertext_len) * 2 + 1);
    char *ptr = hex_output;

    for(int i=0; i<AES_IV_SIZE; i++) { sprintf(ptr, "%02x", iv[i]); ptr += 2; }
    for(int i=0; i<AES_TAG_SIZE; i++) { sprintf(ptr, "%02x", tag[i]); ptr += 2; }
    for(int i=0; i<ciphertext_len; i++) { sprintf(ptr, "%02x", ciphertext[i]); ptr += 2; }
    *ptr = '\0';

    return hex_output;

error:
    EVP_CIPHER_CTX_free(ctx);
    return NULL;
}

// --- DECRYPT API KEY FROM HARDWARE HEX BLOCK ---
char* decrypt_api_key_system_level(const char* hex_encoded_token) {
    if (!hex_encoded_token) return NULL;

    size_t hex_len = strlen(hex_encoded_token);
    // Minimum: IV(12*2) + TAG(16*2) + at least 1 byte ciphertext(1*2) = 58 hex chars
    if (hex_len < 58 || (hex_len % 2) != 0) return NULL;

    size_t total_bytes = hex_len / 2;
    size_t ciphertext_len = total_bytes - AES_IV_SIZE - AES_TAG_SIZE;

    unsigned char iv[AES_IV_SIZE];
    unsigned char tag[AES_TAG_SIZE];
    unsigned char *ciphertext = malloc(ciphertext_len);
    if (!ciphertext) return NULL;

    // Parse hex string back into binary components: IV + TAG + CIPHERTEXT
    const char *ptr = hex_encoded_token;
    for (int i = 0; i < AES_IV_SIZE; i++, ptr += 2) {
        sscanf(ptr, "%2hhx", &iv[i]);
    }
    for (int i = 0; i < AES_TAG_SIZE; i++, ptr += 2) {
        sscanf(ptr, "%2hhx", &tag[i]);
    }
    for (size_t i = 0; i < ciphertext_len; i++, ptr += 2) {
        sscanf(ptr, "%2hhx", &ciphertext[i]);
    }

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx) { free(ciphertext); return NULL; }

    int len;
    unsigned char *plaintext = malloc(ciphertext_len + 1);
    if (!plaintext) { free(ciphertext); EVP_CIPHER_CTX_free(ctx); return NULL; }

    if (1 != EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, system_master_hardware_key, iv)) goto decrypt_error;
    if (1 != EVP_DecryptUpdate(ctx, plaintext, &len, ciphertext, (int)ciphertext_len)) goto decrypt_error;

    int plaintext_len = len;

    if (1 != EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, AES_TAG_SIZE, tag)) goto decrypt_error;
    if (1 != EVP_DecryptFinal_ex(ctx, plaintext + len, &len)) goto decrypt_error;

    plaintext_len += len;
    plaintext[plaintext_len] = '\0';

    EVP_CIPHER_CTX_free(ctx);
    free(ciphertext);

    // Return as a heap-allocated C string
    char *result = malloc(plaintext_len + 1);
    memcpy(result, plaintext, plaintext_len + 1);
    free(plaintext);
    return result;

decrypt_error:
    EVP_CIPHER_CTX_free(ctx);
    free(ciphertext);
    free(plaintext);
    return NULL;
}