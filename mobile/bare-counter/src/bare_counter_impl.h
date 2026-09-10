#pragma once

// The counter the mobile host bundles: a leaf `universal` module, which means
// the whole file is Qt-free and the builder derives the LIDL contract from
// these signatures. The `bare` output of that build is an iOS embedded
// framework and an Android .so — the artifact this app carries.
//
// It lives here rather than being pulled from somewhere else because it is
// what the HOST demonstrates. Until the catalog-driven Bundled-set build
// exists, the app's bundled module list is this one directory.

#include <cstdint>

#include <logos_module_context.h>

class BareCounterImpl : public LogosModuleContext {
public:
    BareCounterImpl() = default;
    ~BareCounterImpl() = default;

    // Stateless, and that is why the host calls it: `add(1, 2)` exercises the
    // whole path — LogosAPI, the generic host glue, logos_module_dispatch, the
    // module's own code and back — in one call whose right answer does not
    // depend on how many times it has been called.
    int64_t add(int64_t a, int64_t b);

    int64_t increment(int64_t amount);
    int64_t current();
    void reset();

logos_events:
    void counted(int64_t value);
};
