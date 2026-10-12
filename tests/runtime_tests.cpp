#include "runtime/PlayerSnapshot.h"
#include "runtime/SignatureCatalog.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {
void check(bool value, const char *message) {
    if (!value) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}
}

int main() {
    using namespace melonrind::runtime;

    auto *primary = signatureSetForVersion("1.26.51.1");
    check(primary && primary->minecraftVersion == "1.26.51",
          "hotfix version resolves primary catalog");
    check(primary && primary->hungerRenderSlot == 17 &&
              primary->heartRenderSlot == 17,
          "primary catalog carries validated HUD vtable slots");
    check(signatureSetForVersion("1.26.53.1") == nullptr,
          "unsupported version is rejected");

    PlayerSnapshot snapshot;
    snapshot.playerIdentity = 1;
    snapshot.runtimeId = 2;
    snapshot.hunger = 40.0f;
    snapshot.maxHunger = 20.0f;
    snapshot.health = 10.0f;
    snapshot.maxHealth = std::nanf("");
    snapshot.mainHandFood = FoodSnapshot{8, 0.8f, false, false, "minecraft:porkchop"};
    snapshot = sanitizeSnapshot(snapshot);
    check(snapshot.hunger == 20.0f && snapshot.health == 10.0f,
          "invalid numeric state is clamped");
    check(snapshot.mainHandFood.has_value(), "valid food survives sanitization");

    snapshot.mainHandFood = FoodSnapshot{8, 0.8f, false, false, "minecraft:porkchop"};
    snapshot.alive = false;
    snapshot = sanitizeSnapshot(snapshot);
    check(!snapshot.mainHandFood.has_value(), "dead player has no food preview");

    std::cout << "Passed runtime checks\n";
}
