/**
 * @file server/channel/src/PlasmaState.cpp
 * @ingroup channel
 *
 * @author HACKfrost
 *
 * @brief Represents the state of a plasma spawn on the channel.
 *
 * This file is part of the Channel Server (channel).
 *
 * Copyright (C) 2012-2020 COMP_hack Team <compomega@tutanota.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "PlasmaState.h"

// libcomp Includes
#include <Packet.h>
#include <PacketCodes.h>
#include <ScriptEngine.h>

// objects Includes
#include <Loot.h>
#include <LootBox.h>
#include <PlasmaSpawn.h>

// channel Includes
#include "ChannelServer.h"

using namespace channel;

namespace libcomp {
template <>
BaseScriptEngine& BaseScriptEngine::Using<PlasmaState>() {
  if (!BindingExists("PlasmaState", true)) {
    Using<objects::EntityStateObject>();

    Sqrat::DerivedClass<PlasmaState, objects::EntityStateObject,
                        Sqrat::NoConstructor<PlasmaState>>
        binding(mVM, "PlasmaState");
    binding.Func("IsActive", &PlasmaState::IsActive)
        .Func("Toggle", &PlasmaState::Toggle);

    Bind<PlasmaState>("PlasmaState", binding);
  }

  return *this;
}
}  // namespace libcomp

PlasmaPoint::PlasmaPoint() {
  Refresh();
  mHidden = true;
}

void PlasmaPoint::Refresh() {
  mLooterID = 0;
  mHideTime = 0;
  mHidden = false;
  mOpen = false;
  mLoot = nullptr;
}

int32_t PlasmaPoint::GetState(int32_t looterID) {
  if (mHidden) {
    return 4;  // Hidden
  } else if (!mOpen) {
    return 0;  // Not opened
  } else if (looterID != -1 && looterID == mLooterID) {
    return 3;  // Opened by self
  } else {
    return 2;  // Opened by other player
  }
}

std::shared_ptr<objects::LootBox> PlasmaPoint::GetLoot() const { return mLoot; }

PlasmaState::PlasmaState(const std::shared_ptr<objects::PlasmaSpawn>& plasma)
    : EntityState<objects::PlasmaSpawn>(plasma) {
  mDeactivated = mDisabled = false;
}

bool PlasmaState::CreatePoints() {
  if (mPoints.size() > 0) {
    // Already created
    return false;
  }

  uint8_t maxCount = GetEntity()->GetPointCount();
  for (uint8_t i = 0; i < maxCount; i++) {
    auto point = std::make_shared<PlasmaPoint>();
    point->SetID((uint32_t)(i + 1));

    mPoints[point->GetID()] = point;
  }

  return true;
}

std::shared_ptr<PlasmaPoint> PlasmaState::GetPoint(uint32_t pointID) {
  std::lock_guard<std::mutex> lock(mLock);

  auto it = mPoints.find(pointID);
  return it != mPoints.end() ? it->second : nullptr;
}

std::list<std::shared_ptr<PlasmaPoint>> PlasmaState::GetActivePoints() {
  std::list<std::shared_ptr<PlasmaPoint>> results;

  std::lock_guard<std::mutex> lock(mLock);
  for (auto pair : mPoints) {
    if (!pair.second->mHidden) {
      results.push_back(pair.second);
    }
  }

  return results;
}

bool PlasmaState::IsActive(bool visible) {
  if (visible) {
    return GetActivePoints().size() > 0;
  } else {
    return !mDeactivated;
  }
}

void PlasmaState::Toggle(bool enable, bool activation) {
  std::lock_guard<std::mutex> lock(mLock);
  if (activation) {
    if (mDeactivated == enable) {
      mDeactivated = !enable;
      return;
    }
  } else if (mDisabled == enable) {
    mDisabled = !enable;
    if (!enable) {
      for (auto& pair : mPoints) {
        mPointHides[pair.second->GetID()] = 0;
      }
    }
  }
}

bool PlasmaState::HasStateChangePoints(bool respawn, uint64_t now) {
  if (now == 0) {
    now = ChannelServer::GetServerTime();
  }

  std::lock_guard<std::mutex> lock(mLock);

  if (respawn) {
    // Do not spawn if enabled but still deactivated
    if (mDisabled || mDeactivated) {
      return false;
    }

    for (auto pair : mPoints) {
      if (pair.second->mHidden) {
        auto rIter = mPointRespawns.find(pair.second->GetID());
        if (rIter == mPointRespawns.end() || rIter->second <= now) {
          return true;
        }
      }
    }
  } else {
    for (auto pair : mPointHides) {
      if (pair.second <= now) {
        return true;
      }
    }
  }

  return false;
}

std::list<std::shared_ptr<PlasmaPoint>> PlasmaState::PopRespawnPoints(
    uint64_t now) {
  if (now == 0) {
    now = ChannelServer::GetServerTime();
  }

  std::list<std::shared_ptr<PlasmaPoint>> results;

  std::lock_guard<std::mutex> lock(mLock);
  for (auto pair : mPoints) {
    if (pair.second->mHidden) {
      auto rIter = mPointRespawns.find(pair.second->GetID());
      if (rIter == mPointRespawns.end() || rIter->second <= now) {
        results.push_back(pair.second);
        mPointRespawns.erase(pair.second->GetID());
      }
    }
  }

  return results;
}

std::list<std::shared_ptr<PlasmaPoint>> PlasmaState::PopHidePoints(
    uint64_t now) {
  if (now == 0) {
    now = ChannelServer::GetServerTime();
  }

  std::list<std::shared_ptr<PlasmaPoint>> results;

  std::lock_guard<std::mutex> lock(mLock);
  for (auto pair : mPoints) {
    if (!pair.second->mHidden) {
      auto rIter = mPointHides.find(pair.second->GetID());
      if (rIter != mPointHides.end() && rIter->second <= now) {
        HidePoint(pair.second);

        results.push_back(pair.second);
        mPointHides.erase(pair.second->GetID());
      }
    }
  }

  return results;
}

bool PlasmaState::PickPoint(const std::shared_ptr<PlasmaPoint>& point,
                            int32_t looterID) {
  std::lock_guard<std::mutex> lock(mLock);

  if (!point) {
    return false;
  }

  // Return false if the point is already being looted or is
  // not currently active
  if ((point->mLooterID != 0 && point->mLooterID != looterID) ||
      point->GetState(looterID) != 0) {
    return false;
  }

  // Point is valid, mark state, set looter ID and return true
  point->mLooterID = looterID;

  return true;
}

std::shared_ptr<PlasmaPoint> PlasmaState::SetPickResult(uint32_t pointID,
                                                        int32_t looterID,
                                                        int8_t result) {
  std::lock_guard<std::mutex> lock(mLock);

  std::shared_ptr<PlasmaPoint> point;
  if (pointID) {
    // Get specific point
    auto it = mPoints.find(pointID);
    if (it == mPoints.end()) {
      return nullptr;
    }

    point = it->second;
  } else {
    // Get current point
    for (auto& pair : mPoints) {
      if (pair.second->mLooterID == looterID) {
        point = pair.second;
        break;
      }
    }
  }

  // Stop if there is no valid point, or if the point is inactive or currently
  // being looted by another
  if (!point || (point->mLooterID != 0 && point->mLooterID != looterID) ||
      point->GetState(looterID) != 0) {
    return nullptr;
  }

  // The result is a relative distance from the center of the "minigame"
  if (result >= 0) {
    point->mOpen = true;

    // Plasma is lootable for 2 minutes
    point->mHideTime = (uint64_t)(ChannelServer::GetServerTime() + 120000000);

    mPointHides[point->GetID()] = point->mHideTime;
  } else {
    HidePoint(point);
  }

  return point;
}

bool PlasmaState::HideIfEmpty(const std::shared_ptr<PlasmaPoint>& point) {
  std::lock_guard<std::mutex> lock(mLock);
  auto it = point ? mPoints.find(point->GetID()) : mPoints.end();
  if (it != mPoints.end() && it->second == point) {
    for (auto l : point->GetLoot()->GetLoot()) {
      if (l && l->GetCount() > 0) {
        return false;
      }
    }

    HidePoint(point);

    return true;
  }

  return false;
}

bool PlasmaState::SetLoot(uint32_t pointID, int32_t looterID,
                          const std::shared_ptr<objects::LootBox>& loot) {
  std::lock_guard<std::mutex> lock(mLock);

  auto it = mPoints.find(pointID);
  if (it == mPoints.end()) {
    return false;
  }

  auto point = it->second;
  if (point->mLooterID != looterID || point->mLoot) {
    return false;
  }

  loot->SetLootTime(point->mHideTime);
  point->mLoot = loot;

  return true;
}

void PlasmaState::GetPointStatusData(libcomp::Packet& p, uint32_t pointID,
                                     int32_t looterID) {
  std::list<uint32_t> pointIDs;
  pointIDs.push_back(pointID);

  GetPointStatusData(p, pointIDs, looterID);
}

void PlasmaState::GetPointStatusData(libcomp::Packet& p,
                                     std::list<uint32_t> pointIDs,
                                     int32_t looterID) {
  p.WritePacketCode(ChannelToClientPacketCode_t::PACKET_PLASMA_STATUS);
  p.WriteS32Little(GetEntityID());
  p.WriteS8((int8_t)pointIDs.size());

  std::lock_guard<std::mutex> lock(mLock);

  for (uint32_t pointID : pointIDs) {
    auto it = mPoints.find(pointID);
    if (it != mPoints.end()) {
      p.WriteS8((int8_t)it->second->GetID());
      p.WriteS32Little(it->second->GetState(looterID));
    } else {
      p.WriteS8(0);
      p.WriteS32Little(0);
    }
  }
}

void PlasmaState::HidePoint(const std::shared_ptr<PlasmaPoint>& point) {
  point->mHidden = true;
  mPointHides.erase(point->GetID());

  if (!mDisabled) {
    uint64_t respawnTime =
        (uint64_t)(ChannelServer::GetServerTime() +
                   (uint64_t)(GetEntity()->GetRespawnTime() * 1000000.f));
    mPointRespawns[point->GetID()] = respawnTime;
  } else {
    mPointRespawns[point->GetID()] = 0;
  }
}
