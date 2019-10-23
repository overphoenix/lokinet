#include <crypto/crypto_libsodium.hpp>

#include <iostream>

#include <gtest/gtest.h>

namespace llarp
{
  struct IdentityKeyTest : public ::testing::Test
  {
    llarp::sodium::CryptoLibSodium crypto;

    IdentityKeyTest()
    {
    }
  };

  TEST_F(IdentityKeyTest, TestKeyGen)
  {
    SecretKey secret;
    ASSERT_TRUE(crypto.identity_keygen(secret));
  }

  TEST_F(IdentityKeyTest, TestSignVerify)
  {
    SecretKey secret;
    ASSERT_TRUE(crypto.identity_keygen(secret));
    AlignedBuffer< 128 > random;
    random.Randomize();
    Signature sig;

    llarp_buffer_t buf(random);
    ASSERT_TRUE(crypto.sign(sig, secret, buf));
    ASSERT_TRUE(crypto.verify(secret.toPublic(), buf, sig));
    random.Randomize();
    // mangle body
    ASSERT_FALSE(crypto.verify(secret.toPublic(), buf, sig));
  }

  struct PQCryptoTest : public ::testing::Test
  {
    llarp::sodium::CryptoLibSodium crypto;
    PQKeyPair keys;

    PQCryptoTest()
    {
    }

    void
    SetUp()
    {
      crypto.pqe_keygen(keys);
    }
  };

  TEST_F(PQCryptoTest, TestCrypto)
  {
    PQCipherBlock block;
    SharedSecret shared, otherShared;
    auto c = &crypto;

    ASSERT_TRUE(keys.size() == PQ_KEYPAIRSIZE);
    ASSERT_TRUE(
        c->pqe_encrypt(block, shared, PQPubKey(pq_keypair_to_public(keys))));
    ASSERT_TRUE(c->pqe_decrypt(block, otherShared, pq_keypair_to_secret(keys)));
    ASSERT_TRUE(otherShared == shared);
  }
}  // namespace llarp
