/* Unit tests for VIGIL URL parsing library. */
#include "vigil/url.h"
#include "vigil_test.h"

#include <stdlib.h>
#include <string.h>

/* ── Parse Tests ─────────────────────────────────────────────────── */

TEST(VigilUrlTest, ParseFull)
{
    vigil_url_t url;
    vigil_status_t s = vigil_url_parse(NULL, "https://user:pass@example.com:8080/path?query=1#frag", 52, &url, NULL);
    ASSERT_EQ(s, VIGIL_STATUS_OK);
    EXPECT_STREQ(url.scheme, "https");
    EXPECT_STREQ(url.username, "user");
    EXPECT_STREQ(url.password, "pass");
    EXPECT_STREQ(url.host, "example.com");
    EXPECT_STREQ(url.port, "8080");
    EXPECT_STREQ(url.path, "/path");
    EXPECT_STREQ(url.raw_query, "query=1");
    EXPECT_STREQ(url.fragment, "frag");
    vigil_url_free(&url);
}

TEST(VigilUrlTest, ParseSimple)
{
    vigil_url_t url;
    vigil_status_t s = vigil_url_parse(NULL, "http://example.com", 18, &url, NULL);
    ASSERT_EQ(s, VIGIL_STATUS_OK);
    EXPECT_STREQ(url.scheme, "http");
    EXPECT_STREQ(url.host, "example.com");
    EXPECT_EQ(url.port, NULL);
    vigil_url_free(&url);
}

TEST(VigilUrlTest, ParsePathOnly)
{
    vigil_url_t url;
    vigil_status_t s = vigil_url_parse(NULL, "/foo/bar", 8, &url, NULL);
    ASSERT_EQ(s, VIGIL_STATUS_OK);
    EXPECT_EQ(url.scheme, NULL);
    EXPECT_EQ(url.host, NULL);
    EXPECT_STREQ(url.path, "/foo/bar");
    vigil_url_free(&url);
}

TEST(VigilUrlTest, ParseWithQuery)
{
    vigil_url_t url;
    vigil_status_t s = vigil_url_parse(NULL, "http://example.com?a=1&b=2", 26, &url, NULL);
    ASSERT_EQ(s, VIGIL_STATUS_OK);
    EXPECT_STREQ(url.raw_query, "a=1&b=2");
    vigil_url_free(&url);
}

TEST(VigilUrlTest, ParseIPv6)
{
    vigil_url_t url;
    vigil_status_t s = vigil_url_parse(NULL, "http://[::1]:8080/path", 22, &url, NULL);
    ASSERT_EQ(s, VIGIL_STATUS_OK);
    EXPECT_STREQ(url.host, "::1");
    EXPECT_STREQ(url.port, "8080");
    vigil_url_free(&url);
}

/* ── Encoding Tests ──────────────────────────────────────────────── */

TEST(VigilUrlTest, EncodeSpaces)
{
    char *encoded;
    size_t len;
    vigil_status_t s = vigil_url_query_escape(NULL, "hello world", 11, &encoded, &len, NULL);
    ASSERT_EQ(s, VIGIL_STATUS_OK);
    EXPECT_STREQ(encoded, "hello+world");
    free(encoded);
}

TEST(VigilUrlTest, EncodeSpecial)
{
    char *encoded;
    size_t len;
    vigil_status_t s = vigil_url_query_escape(NULL, "a=b&c=d", 7, &encoded, &len, NULL);
    ASSERT_EQ(s, VIGIL_STATUS_OK);
    EXPECT_STREQ(encoded, "a%3Db%26c%3Dd");
    free(encoded);
}

TEST(VigilUrlTest, DecodePercent)
{
    char *decoded;
    size_t len;
    vigil_status_t s = vigil_url_unescape(NULL, "hello%20world", 13, &decoded, &len, NULL);
    ASSERT_EQ(s, VIGIL_STATUS_OK);
    EXPECT_STREQ(decoded, "hello world");
    free(decoded);
}

TEST(VigilUrlTest, DecodePlus)
{
    char *decoded;
    size_t len;
    vigil_status_t s = vigil_url_unescape(NULL, "hello+world", 11, &decoded, &len, NULL);
    ASSERT_EQ(s, VIGIL_STATUS_OK);
    EXPECT_STREQ(decoded, "hello world");
    free(decoded);
}

/* ── Utility Tests ───────────────────────────────────────────────── */

TEST(VigilUrlTest, IsAbsolute)
{
    vigil_url_t url;
    vigil_url_parse(NULL, "https://example.com", 19, &url, NULL);
    EXPECT_TRUE(vigil_url_is_absolute(&url));
    vigil_url_free(&url);

    vigil_url_parse(NULL, "/path/only", 10, &url, NULL);
    EXPECT_FALSE(vigil_url_is_absolute(&url));
    vigil_url_free(&url);
}

TEST(VigilUrlTest, Hostname)
{
    vigil_url_t url;
    vigil_url_parse(NULL, "https://example.com:8080/path", 29, &url, NULL);
    EXPECT_STREQ(vigil_url_hostname(&url), "example.com");
    vigil_url_free(&url);
}

/* ── Test Registration ───────────────────────────────────────────── */

void register_url_tests(void)
{
    REGISTER_TEST(VigilUrlTest, ParseFull);
    REGISTER_TEST(VigilUrlTest, ParseSimple);
    REGISTER_TEST(VigilUrlTest, ParsePathOnly);
    REGISTER_TEST(VigilUrlTest, ParseWithQuery);
    REGISTER_TEST(VigilUrlTest, ParseIPv6);
    REGISTER_TEST(VigilUrlTest, EncodeSpaces);
    REGISTER_TEST(VigilUrlTest, EncodeSpecial);
    REGISTER_TEST(VigilUrlTest, DecodePercent);
    REGISTER_TEST(VigilUrlTest, DecodePlus);
    REGISTER_TEST(VigilUrlTest, IsAbsolute);
    REGISTER_TEST(VigilUrlTest, Hostname);
}
