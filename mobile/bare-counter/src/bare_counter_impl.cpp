#include "bare_counter_impl.h"

namespace {
int64_t g_value = 0;
}

int64_t BareCounterImpl::add(int64_t a, int64_t b)
{
    return a + b;
}

int64_t BareCounterImpl::increment(int64_t amount)
{
    g_value += amount;
    counted(g_value);
    return g_value;
}

int64_t BareCounterImpl::current()
{
    return g_value;
}

void BareCounterImpl::reset()
{
    g_value = 0;
}
