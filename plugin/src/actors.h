#pragma once

#include "cmp_protocol.hpp"

void CMP_SendHostActors();
void CMP_ApplyHostActors();
void CMP_ClearHostActors();
void CMP_OnActorPose(const cmp::ActorPose& pose);
