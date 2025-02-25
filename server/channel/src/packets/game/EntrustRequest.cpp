/**
 * @file server/channel/src/packets/game/EntrustRequest.cpp
 * @ingroup channel
 *
 * @author HACKfrost
 *
 * @brief Request from the client to start a player exchange "entrust" session.
 *  These sessions include demon crystallization as well as tarot and soul
 *  enchantment.
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
#include <ErrorCodes.h>
#include <Log.h>
#include <ManagerPacket.h>
#include <Packet.h>
#include <PacketCodes.h>
#include <ServerConstants.h>

// object Includes
#include <PlayerExchangeSession.h>

// channel Includes
#include "ChannelServer.h"
#include "CharacterManager.h"
#include "ManagerConnection.h"

using namespace channel;

bool Parsers::EntrustRequest::Parse(
    libcomp::ManagerPacket* pPacketManager,
    const std::shared_ptr<libcomp::TcpConnection>& connection,
    libcomp::ReadOnlyPacket& p) const {
  if (p.Size() != 8) {
    return false;
  }

  uint32_t skillID = p.ReadU32Little();
  int32_t targetEntityID = p.ReadS32Little();

  objects::PlayerExchangeSession::Type_t sessionType;
  if (skillID == SVR_CONST.SYNTH_SKILLS[0]) {
    sessionType = objects::PlayerExchangeSession::Type_t::CRYSTALLIZE;
  } else if (skillID == SVR_CONST.SYNTH_SKILLS[1]) {
    sessionType = objects::PlayerExchangeSession::Type_t::ENCHANT_TAROT;
  } else if (skillID == SVR_CONST.SYNTH_SKILLS[2]) {
    sessionType = objects::PlayerExchangeSession::Type_t::ENCHANT_SOUL;
  } else if (skillID == SVR_CONST.SYNTH_SKILLS[3]) {
    sessionType = objects::PlayerExchangeSession::Type_t::SYNTH_MELEE;
  } else if (skillID == SVR_CONST.SYNTH_SKILLS[4]) {
    sessionType = objects::PlayerExchangeSession::Type_t::SYNTH_GUN;
  } else {
    LogGeneralError([&]() {
      return libcomp::String("Invalid entrust skill supplied: %1\n")
          .Arg(skillID);
    });

    return false;
  }

  auto server =
      std::dynamic_pointer_cast<ChannelServer>(pPacketManager->GetServer());
  auto characterManager = server->GetCharacterManager();
  auto client = std::dynamic_pointer_cast<ChannelClientConnection>(connection);
  auto state = client->GetClientState();
  auto cState = state->GetCharacterState();

  auto targetClient = cState->GetEntityID() == targetEntityID
                          ? client
                          : server->GetManagerConnection()->GetEntityClient(
                                (int32_t)targetEntityID);
  auto targetState = targetClient ? targetClient->GetClientState() : nullptr;
  auto targetCState = targetState ? targetState->GetCharacterState() : nullptr;

  libcomp::Packet reply;
  reply.WritePacketCode(ChannelToClientPacketCode_t::PACKET_ENTRUST_REQUEST);

  // Entrust requests are supposed to have a maximum distance.
  if (!targetClient || state->GetExchangeSession() ||
      targetClient->GetClientState()->GetExchangeSession() ||
      (targetClient != client &&
       cState->GetDistance(targetCState->GetCurrentX(),
                           targetCState->GetCurrentY()) >
           MAX_ENTRUST_DISTANCE)) {
    reply.WriteS32Little(-1);

    client->SendPacket(reply);

    return true;
  }

  EntrustErrorCodes_t responseCode = EntrustErrorCodes_t::SUCCESS;

  if (sessionType == objects::PlayerExchangeSession::Type_t::CRYSTALLIZE) {
    auto targetDemon = targetState->GetDemonState()->GetEntity();
    if (!targetDemon) {
      responseCode = EntrustErrorCodes_t::INVALID_CHAR_STATE;
    } else if (characterManager->GetFamiliarityRank(
                   targetDemon->GetFamiliarity()) < 3) {
      responseCode = EntrustErrorCodes_t::INVALID_DEMON_TARGET;
    } else {
      // Make sure the demon has not been reunioned
      for (int8_t reunionRank : targetDemon->GetReunion()) {
        if (reunionRank) {
          responseCode = EntrustErrorCodes_t::INVALID_DEMON_TARGET;
          break;
        }
      }
    }
  }

  if (responseCode == EntrustErrorCodes_t::SUCCESS) {
    // Set the exchange session info
    auto exchangeSession = std::make_shared<objects::PlayerExchangeSession>();
    exchangeSession->SetSourceEntityID(cState->GetEntityID());
    exchangeSession->SetType(sessionType);
    if (targetClient != client) {
      exchangeSession->SetOtherCharacterState(targetCState);

      // When synth is another character, share the exchange session
      targetState->SetExchangeSession(exchangeSession);

      libcomp::Packet request;
      request.WritePacketCode(
          ChannelToClientPacketCode_t::PACKET_ENTRUST_REQUESTED);
      request.WriteU32Little(skillID);
      request.WriteS32Little(cState->GetEntityID());

      targetClient->SendPacket(request);
    } else {
      // Synth is the same character
      exchangeSession->SetOtherCharacterState(cState);

      server->GetCharacterManager()->SetStatusIcon(client, 8);
    }

    state->SetExchangeSession(exchangeSession);
  }

  reply.WriteS32Little((int32_t)responseCode);

  client->SendPacket(reply);

  return true;
}
