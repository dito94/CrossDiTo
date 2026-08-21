#pragma once

// User-facing product name. Legacy CROSSINK_* build symbols remain below for
// compatibility with existing build overrides and persisted data.
#ifndef CROSSDITO_PRODUCT_NAME
#define CROSSDITO_PRODUCT_NAME "CrossDiTo"
#endif

// Preserve the upstream lineage in user-visible build information without
// mixing it into the semantic version consumed by OTA comparisons.
#ifndef CROSSDITO_UPSTREAM_PRODUCT_NAME
#define CROSSDITO_UPSTREAM_PRODUCT_NAME "CrossInk"
#endif

#ifndef CROSSDITO_UPSTREAM_VERSION
#define CROSSDITO_UPSTREAM_VERSION "1.5.0"
#endif

// PlatformIO normally supplies these through build_flags/extra_scripts. Keep
// fallbacks here so editor indexers and simulator-like tools still parse files.
#ifndef CROSSINK_VERSION
#define CROSSINK_VERSION "dev"
#endif

#ifndef CROSSINK_BUILD_ENV
#define CROSSINK_BUILD_ENV "unknown"
#endif

#ifndef CROSSINK_FIRMWARE_DEVICE_TYPE
#define CROSSINK_FIRMWARE_DEVICE_TYPE "unknown"
#endif

#ifndef CROSSDITO_VERSION_PROVENANCE_LABEL
#define CROSSDITO_VERSION_PROVENANCE_LABEL \
  CROSSDITO_PRODUCT_NAME " " CROSSINK_VERSION " / " CROSSDITO_UPSTREAM_PRODUCT_NAME " " CROSSDITO_UPSTREAM_VERSION
#endif

#ifndef CROSSDITO_HTTP_USER_AGENT
#define CROSSDITO_HTTP_USER_AGENT CROSSDITO_PRODUCT_NAME "-X4-Pro/" CROSSINK_VERSION
#endif
