#pragma once
#include <cstdio>
#include <cstdlib>
static int g_failed = 0, g_checks = 0;
#define CHECK(c) do { ++g_checks; if (!(c)) { ++g_failed; std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)
#define TEST_MAIN_END() do { std::printf("%s: %d checks, %d failed\n", __FILE__, g_checks, g_failed); return g_failed ? 1 : 0; } while (0)
