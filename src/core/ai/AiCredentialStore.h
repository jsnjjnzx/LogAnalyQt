#pragma once

#include "../SecureCredentialStore.h"

// Preserve existing AI profiles and test injection while sharing native storage.
class AiCredentialStore : public SecureCredentialStore {
public:
  AiCredentialStore() : SecureCredentialStore(Domain::AiFraming) {}
};
