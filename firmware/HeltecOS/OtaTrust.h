#pragma once
// Public OTA trust anchor. Never put a private key in this file.
#define HELTEC_OS_SIGNING_KEY_ID "794fd15a48559d2cb4d149df975c6279382cce4c3c9be4c8c4ca202beb87df9f"
namespace OtaTrust {
inline constexpr char PUBLIC_KEY[] = R"KEY(-----BEGIN PUBLIC KEY-----
MIIBojANBgkqhkiG9w0BAQEFAAOCAY8AMIIBigKCAYEAm6VbQzidYoNw9aEnROnx
q0RpWfE86wDt+SAMcW8JD+Mo/uERr0C4dtSiEeXQM2z2d1SaQIE7yMKcoRIus5cJ
YUDaNrGCWGfzVeloxMWnCt2u2aMvXJS75znAgwAck6VjKYG4nPwdA3GkRuXhbvJe
D1bWR8w8CI1ju0cYR7oxnZ2Jfj9jtCSlO4JpvFW9PgJnaPteLP6TLlW6n8rbtadk
7DtFim81zzfJoeyKhs/5nemQb45V2ixcuQcBXNx5wDtE1rsSsrF/KsqWwjX8i7QK
COXsTrLAhhvZZQrr/CXojPtOX68+Zw2pETXuaAtjtCmHTA+T3OKVzi/9z9Z0eP4g
MHIHr5Ck2CfZkg0ALd8Y/AZS+vMCsiPiqdmZb7mEcZuC9zfUR8aXuv7JETrER+B9
rKDfshHxrw7Z5o7IC0oSp65hLKyhovwi98RuPUIvkjR7AvuBnj6v608hP+j3rHTt
UhqqrwstvY/6mT8d8kc82Fs6A3cKAEp5hBKW39hffBIhAgMBAAE=
-----END PUBLIC KEY-----
)KEY";
}
