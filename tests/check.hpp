#pragma once
#include <stdio.h>

// Minimal test helpers: CHECK counts failures and goes on, TEST names a test function.
extern int g_checks;
extern int g_failures;

#define CHECK(cond) \
	do { \
		g_checks++; \
		if (!(cond)) { \
			g_failures++; \
			printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
		} \
	} while (0)

#define CHECK_EQ(a, b) \
	do { \
		g_checks++; \
		long long va_ = (long long) (a); \
		long long vb_ = (long long) (b); \
		if (va_ != vb_) { \
			g_failures++; \
			printf("  FAIL %s:%d: %s == %s (%lld vs %lld)\n", __FILE__, __LINE__, #a, #b, va_, vb_); \
		} \
	} while (0)

#define CHECK_NEAR(a, b, eps) \
	do { \
		g_checks++; \
		double va_ = (double) (a); \
		double vb_ = (double) (b); \
		if (va_ - vb_ > (eps) || vb_ - va_ > (eps)) { \
			g_failures++; \
			printf("  FAIL %s:%d: %s ~ %s (%g vs %g)\n", __FILE__, __LINE__, #a, #b, va_, vb_); \
		} \
	} while (0)
