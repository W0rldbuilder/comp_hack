/**
 * @file server/channel/src/packets/game/LootTreasureBox.cpp
 * @ingroup channel
 *
 * @author HACKfrost
 *
 * @brief Request from the client for the list of items inside a
 *  treasure loot box.
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

#include "Packets.h"

// libcomp Includes
#include <ManagerPacket.h>
#include <Packet.h>
#include <PacketCodes.h>

// libhack includes
#include <Log.h>

// object Includes
#include <Loot.h>
#include <LootBox.h>

// channel Includes
#include "ChannelServer.h"
#include "CharacterManager.h"

using namespace channel;

bool Parsers::LootTreasureBox::Parse(
    libcomp::ManagerPacket* pPacketManager,
    const std::shared_ptr<libcomp::TcpConnection>& connection,
    libcomp::ReadOnlyPacket& p) const {
  if (p.Size() != 8) {
    return false;
  }

  int32_t entityID = p.ReadS32Little();
  int32_t lootEntityID = p.ReadS32Little();

  auto client = std::dynamic_pointer_cast<ChannelClientConnection>(connection);
  auto server =
      std::dynamic_pointer_cast<ChannelServer>(pPacketManager->GetServer());
  auto characterManager = server->GetCharacterManager();

  auto state = client->GetClientState();
  auto cState = state->GetCharacterState();
  auto zone = cState->GetZone();
  auto lState = zone ? zone->GetLootBox(lootEntityID) : nullptr;
  auto lBox = lState ? lState->GetEntity() : nullptr;

  libcomp::Packet reply;
  reply.WritePacketCode(ChannelToClientPacketCode_t::PACKET_LOOT_TREASURE_BOX);
  reply.WriteS32Little(entityID);
  reply.WriteS32Little(lootEntityID);

  if (lBox && !cState->CanInteract(lState, MAX_LOOT_DISTANCE)) {
    // They can't actually make this interaction. Send a failure notification.
    LogGeneralWarning([&]() {
      return libcomp::String(
                 "Player is either too far from a lootbox in zone %1 "
                 "(distance: %2) to loot it or does not have line of "
                 "sight to it: %3\n")
          .Arg(zone->GetDefinitionID())
          .Arg(
              cState->GetDistance(lState->GetCurrentX(), lState->GetCurrentY()))
          .Arg(state->GetAccountUID().ToString());
    });

    reply.WriteS8(-1);  // Failure

    client->SendPacket(reply);
  } else if (lBox && ((lBox->ValidLooterIDsCount() == 0 &&
                       lBox->GetType() != objects::LootBox::Type_t::BOSS_BOX) ||
                      lBox->ValidLooterIDsContains(state->GetWorldCID()))) {
    reply.WriteS8(0);  // Success

    client->QueuePacket(reply);

    std::list<std::shared_ptr<ChannelClientConnection>> clients = {client};
    characterManager->SendLootItemData(clients, lState);
  } else {
    reply.WriteS8(-1);  // Failure

    client->SendPacket(reply);
  }

  return true;
}
