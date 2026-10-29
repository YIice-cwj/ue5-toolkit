#include "NetSmoke/ANetSmokeGameMode.h"

#include "NetSmoke/ANetSmokePawn.h"
#include "NetSmoke/ANetSmokePlayerController.h"

ANetSmokeGameMode::ANetSmokeGameMode() {
    DefaultPawnClass = ANetSmokePawn::StaticClass();
    PlayerControllerClass = ANetSmokePlayerController::StaticClass();
}

