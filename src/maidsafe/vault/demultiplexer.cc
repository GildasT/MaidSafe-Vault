/***************************************************************************************************
 *  Copyright 2012 MaidSafe.net limited                                                            *
 *                                                                                                 *
 *  The following source code is property of MaidSafe.net limited and is not meant for external    *
 *  use.  The use of this code is governed by the licence file licence.txt found in the root of    *
 *  this directory and also on www.maidsafe.net.                                                   *
 *                                                                                                 *
 *  You are not free to copy, amend or otherwise use this source code without the explicit         *
 *  written permission of the board of directors of MaidSafe.net.                                  *
 **************************************************************************************************/


#include "maidsafe/vault/demultiplexer.h"

#include "maidsafe/common/log.h"
#include "maidsafe/passport/types.h"
#include "maidsafe/detail/data_type_values.h"

#include "maidsafe/vault/data_holder/data_holder.h"
#include "maidsafe/vault/maid_account_holder/maid_account_holder.h"
#include "maidsafe/vault/metadata_manager/metadata_manager.h"
#include "maidsafe/vault/pmid_account_holder/pmid_account_holder.h"


namespace maidsafe {

namespace vault {

namespace {

template<typename PersonaType>
void HandleDataType(nfs::Message& message,
                    const routing::ReplyFunctor& reply_functor,
                    PersonaType& persona_type) {
  // static assert
  switch (message.data_type()) {
    case maidsafe::detail::DataTagValue::kAnmidValue:
      persona_type.template HandleMessage<passport::PublicAnmid>(message, reply_functor);
      break;
    case maidsafe::detail::DataTagValue::kAnsmidValue:
      persona_type.template HandleMessage<passport::PublicAnsmid>(message, reply_functor);
      break;
    case maidsafe::detail::DataTagValue::kAntmidValue:
      persona_type.template HandleMessage<passport::PublicAntmid>(message, reply_functor);
      break;
    case maidsafe::detail::DataTagValue::kAnmaidValue:
      persona_type.template HandleMessage<passport::PublicAnmaid>(message, reply_functor);
      break;
    case maidsafe::detail::DataTagValue::kMaidValue:
      persona_type.template HandleMessage<passport::PublicMaid>(message, reply_functor);
      break;
    case maidsafe::detail::DataTagValue::kPmidValue:
      persona_type.template HandleMessage<passport::PublicPmid>(message, reply_functor);
      break;
    case maidsafe::detail::DataTagValue::kMidValue:
      persona_type.template HandleMessage<passport::Mid>(message, reply_functor);
      break;
    case maidsafe::detail::DataTagValue::kSmidValue:
      persona_type.template HandleMessage<passport::Smid>(message, reply_functor);
      break;
    case maidsafe::detail::DataTagValue::kTmidValue:
      persona_type.template HandleMessage<passport::Tmid>(message, reply_functor);
      break;
    case maidsafe::detail::DataTagValue::kAnmpidValue:
      persona_type.template HandleMessage<passport::PublicAnmpid>(message, reply_functor);
      break;
    case maidsafe::detail::DataTagValue::kMpidValue:
      persona_type.template HandleMessage<passport::PublicMpid>(message, reply_functor);
      break;
    case maidsafe::detail::DataTagValue::kImmutableDataValue:
      persona_type.template HandleMessage<ImmutableData>(message, reply_functor);
      break;
    case maidsafe::detail::DataTagValue::kMutableDataValue:
      persona_type.template HandleMessage<MutableData>(message, reply_functor);
      break;
    // case static_cast<int>(DataTagValue::kMessageDataValue):
    //   persona_type.template HandlePostMessage(message, reply_functor);
    //   break;
    default :
      LOG(kError) << "Unhandled data type";
  }
}

}  // unnamed namespace


Demultiplexer::Demultiplexer(MaidAccountHolder& maid_account_holder,
                             MetadataManager& metadata_manager,
                             PmidAccountHolder& pmid_account_holder,
                             DataHolder& data_holder)
    : maid_account_holder_(maid_account_holder),
      metadata_manager_(metadata_manager),
      pmid_account_holder_(pmid_account_holder),
      data_holder_(data_holder) {}

void Demultiplexer::HandleMessage(const std::string& serialised_message,
                                  const routing::ReplyFunctor& reply_functor) {
  try {
    nfs::Message message((nfs::Message::serialised_type((NonEmptyString(serialised_message)))));
    HandleMessagePersonaType(message, reply_functor);
  } catch(const std::exception& ex) {
    LOG(kError) << "Caught exception on handling new message: " << ex.what();
  }
}

void Demultiplexer::HandleMessagePersonaType(nfs::Message& message,
                                             const routing::ReplyFunctor& reply_functor) {
  switch (message.destination_persona_type()) {
    case nfs::PersonaType::kMaidAccountHolder :
      HandleDataType<MaidAccountHolder>(message, reply_functor, maid_account_holder_);
      break;
    case nfs::PersonaType::kMetadataManager :
      HandleDataType<MetadataManager>(message, reply_functor, metadata_manager_);
      break;
    case nfs::PersonaType::kPmidAccountHolder :
      HandleDataType<PmidAccountHolder>(message, reply_functor, pmid_account_holder_);
      break;
    case nfs::PersonaType::kDataHolder :
      HandleDataType<DataHolder>(message, reply_functor, data_holder_);
      break;
    default :
      LOG(kError) << "Unhandled PersonaType";
  }
}

bool Demultiplexer::GetFromCache(std::string& serialised_message) {
  try {
    nfs::Message request((nfs::Message::serialised_type((NonEmptyString(serialised_message)))));
    auto cached_content(HandleGetFromCache(request));
    if (cached_content.IsInitialised()) {
      nfs::Message message(request.action_type(),
                           request.destination_persona_type(),
                           request.source(),
                           request.data_type(),
                           request.name(),
                           cached_content,
                           asymm::Signature());
      serialised_message = message.Serialise()->string();
      return true;
    }
  } catch(const std::exception& ex) {
    LOG(kError) << "Caught exception on handling get from cache request: " << ex.what();
  }
  return false;
}

NonEmptyString Demultiplexer::HandleGetFromCache(nfs::Message& message) {
  switch (message.data_type()) {
    case maidsafe::detail::DataTagValue::kAnmidValue:
      return data_holder_.GetFromCache<passport::PublicAnmid>(message);
    case maidsafe::detail::DataTagValue::kAnsmidValue:
      return data_holder_.GetFromCache<passport::PublicAnsmid>(message);
    case maidsafe::detail::DataTagValue::kAntmidValue:
      return data_holder_.GetFromCache<passport::PublicAntmid>(message);
    case maidsafe::detail::DataTagValue::kAnmaidValue:
      return data_holder_.GetFromCache<passport::PublicAnmaid>(message);
    case maidsafe::detail::DataTagValue::kMaidValue:
      return data_holder_.GetFromCache<passport::PublicMaid>(message);
    case maidsafe::detail::DataTagValue::kPmidValue:
      return data_holder_.GetFromCache<passport::PublicPmid>(message);
    case maidsafe::detail::DataTagValue::kAnmpidValue:
      return data_holder_.GetFromCache<passport::PublicAnmpid>(message);
    case maidsafe::detail::DataTagValue::kMpidValue:
      return data_holder_.GetFromCache<passport::PublicMpid>(message);
    case maidsafe::detail::DataTagValue::kImmutableDataValue:
      return data_holder_.GetFromCache<ImmutableData>(message);
    case maidsafe::detail::DataTagValue::kMutableDataValue:
      return data_holder_.GetFromCache<MutableData>(message);
    default :
      LOG(kError) << "Unhandled data type";
  }
  return NonEmptyString();
}

void Demultiplexer::StoreInCache(const std::string& serialised_message) {
  try {
    nfs::Message message((nfs::Message::serialised_type((NonEmptyString(serialised_message)))));
    HandleStoreInCache(message);
  } catch(const std::exception& ex) {
    LOG(kError) << "Caught exception on handling store in cache request: " << ex.what();
  }
}

void Demultiplexer::HandleStoreInCache(const nfs::Message& message) {
  switch (message.data_type()) {
    case maidsafe::detail::DataTagValue::kAnmidValue:
      return data_holder_.StoreInCache<passport::PublicAnmid>(message);
    case maidsafe::detail::DataTagValue::kAnsmidValue:
      return data_holder_.StoreInCache<passport::PublicAnsmid>(message);
    case maidsafe::detail::DataTagValue::kAntmidValue:
      return data_holder_.StoreInCache<passport::PublicAntmid>(message);
    case maidsafe::detail::DataTagValue::kAnmaidValue:
      return data_holder_.StoreInCache<passport::PublicAnmaid>(message);
    case maidsafe::detail::DataTagValue::kMaidValue:
      return data_holder_.StoreInCache<passport::PublicMaid>(message);
    case maidsafe::detail::DataTagValue::kPmidValue:
      return data_holder_.StoreInCache<passport::PublicPmid>(message);
    case maidsafe::detail::DataTagValue::kAnmpidValue:
      return data_holder_.StoreInCache<passport::PublicAnmpid>(message);
    case maidsafe::detail::DataTagValue::kMpidValue:
      return data_holder_.StoreInCache<passport::PublicMpid>(message);
    case maidsafe::detail::DataTagValue::kImmutableDataValue:
      return data_holder_.StoreInCache<ImmutableData>(message);
    case maidsafe::detail::DataTagValue::kMutableDataValue:
      return data_holder_.StoreInCache<MutableData>(message);
    default :
      LOG(kError) << "Unhandled data type";
  }
}

}  // namespace vault

}  // namespace maidsafe
