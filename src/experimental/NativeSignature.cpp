#if GADGET_OS_NATIVE_APP_LOADER
#include "experimental/NativeSignature.h"
#include <cstring>
#if defined(ARDUINO)
#include <mbedtls/sha256.h>
#include <mbedtls/pk.h>
#else
#include <openssl/evp.h>
#include <openssl/pem.h>
#endif
namespace native_app {
bool sha256_parts(const uint8_t*a,size_t an,const uint8_t*b,size_t bn,const uint8_t*c,size_t cn,uint8_t out[32]) {
#if defined(ARDUINO)
    mbedtls_sha256_context ctx;mbedtls_sha256_init(&ctx);
    bool ok=mbedtls_sha256_starts(&ctx,0)==0;
    if(an)ok=ok&&mbedtls_sha256_update(&ctx,a,an)==0;
    if(bn)ok=ok&&mbedtls_sha256_update(&ctx,b,bn)==0;
    if(cn)ok=ok&&mbedtls_sha256_update(&ctx,c,cn)==0;
    ok=ok&&mbedtls_sha256_finish(&ctx,out)==0;mbedtls_sha256_free(&ctx);return ok;
#else
    auto *ctx=EVP_MD_CTX_new();if(!ctx)return false;
    bool ok=EVP_DigestInit_ex(ctx,EVP_sha256(),nullptr)==1;
    if(an)ok=ok&&EVP_DigestUpdate(ctx,a,an)==1;
    if(bn)ok=ok&&EVP_DigestUpdate(ctx,b,bn)==1;
    if(cn)ok=ok&&EVP_DigestUpdate(ctx,c,cn)==1;
    ok=ok&&EVP_DigestFinal_ex(ctx,out,nullptr)==1;EVP_MD_CTX_free(ctx);return ok;
#endif
}
bool verify_signature(const char *pem,const uint8_t digest[32],const uint8_t signature[64]) {
    // Convert fixed-width r||s to the canonical ASN.1 representation both libraries accept.
    uint8_t der[72];size_t n=2;der[0]=0x30;
    for(unsigned part=0;part<2;++part){
        const uint8_t *p=signature+part*32;size_t len=32;
        while(len>1 && *p==0){++p;--len;}
        bool pad=(*p&0x80)!=0;der[n++]=2;der[n++]=len+pad;
        if(pad)der[n++]=0;memcpy(der+n,p,len);n+=len;
    }
    der[1]=n-2;
#if defined(ARDUINO)
    mbedtls_pk_context key;mbedtls_pk_init(&key);
    bool ok=mbedtls_pk_parse_public_key(&key,(const uint8_t*)pem,strlen(pem)+1)==0 &&
        mbedtls_pk_can_do(&key,MBEDTLS_PK_ECDSA) && mbedtls_pk_get_bitlen(&key)==256 &&
        mbedtls_pk_verify(&key,MBEDTLS_MD_SHA256,digest,32,der,n)==0;
    mbedtls_pk_free(&key);return ok;
#else
    BIO *bio=BIO_new_mem_buf(pem,-1);if(!bio)return false;
    EVP_PKEY *key=PEM_read_bio_PUBKEY(bio,nullptr,nullptr,nullptr);BIO_free(bio);
    if(!key)return false;
    EVP_PKEY_CTX *ctx=EVP_PKEY_CTX_new(key,nullptr);
    bool ok=ctx && EVP_PKEY_base_id(key)==EVP_PKEY_EC && EVP_PKEY_bits(key)==256 &&
        EVP_PKEY_verify_init(ctx)==1 && EVP_PKEY_CTX_set_signature_md(ctx,EVP_sha256())==1 &&
        EVP_PKEY_verify(ctx,der,n,digest,32)==1;
    EVP_PKEY_CTX_free(ctx);EVP_PKEY_free(key);return ok;
#endif
}
}
#endif
