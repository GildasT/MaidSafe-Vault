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

#include "maidsafe/vault/tests/key_helper_operations.h"

#include "boost/filesystem/operations.hpp"

#include "maidsafe/common/test.h"

#include "maidsafe/data_types/immutable_data.h"

#include "maidsafe/routing/node_info.h"
#include "maidsafe/routing/parameters.h"


namespace maidsafe {

namespace vault {

namespace tools {

namespace fs = boost::filesystem;
namespace po = boost::program_options;

typedef boost::asio::ip::udp::endpoint UdpEndpoint;

std::mutex mutex_;
std::condition_variable cond_var_;
bool ctrlc_pressed(false);

void CtrlCHandler(int /*signum*/) {
//   LOG(kInfo) << " Signal received: " << signum;
  std::lock_guard<std::mutex> lock(mutex_);
  ctrlc_pressed = true;
  cond_var_.notify_one();
}

void PrintKeys(const PmidVector &all_pmids) {
  for (size_t i = 0; i < all_pmids.size(); ++i)
    std::cout << '\t' << i << "\t PMID " << HexSubstr(all_pmids[i].name().data.string())
              << (i < 2 ? " (bootstrap)" : "") << std::endl;
}

bool CreateKeys(const size_t &pmids_count, PmidVector &all_pmids) {
  all_pmids.clear();
  for (size_t i = 0; i < pmids_count; ++i) {
    try {
      passport::Anmaid anmaid;
      passport::Maid maid(anmaid);
      passport::Pmid pmid(maid);
      all_pmids.push_back(pmid);
    }
    catch(const std::exception& /*ex*/) {
      LOG(kError) << "CreatePmids - Could not create ID #" << i;
      return false;
    }
  }
  return true;
}

fs::path GetPathFromProgramOption(const std::string &option_name,
                                  po::variables_map *variables_map,
                                  bool is_dir,
                                  bool create_new_if_absent) {
  fs::path option_path;
  if (variables_map->count(option_name))
    option_path = variables_map->at(option_name).as<std::string>();
  if (option_path.empty())
    return fs::path();

  boost::system::error_code ec;
  if (!fs::exists(option_path, ec) || ec) {
    if (!create_new_if_absent) {
      LOG(kError) << "GetPathFromProgramOption - Invalid " << option_name << ", " << option_path
                  << " doesn't exist or can't be accessed (" << ec.message() << ")";
      return fs::path();
    }

    if (is_dir) {  // Create new dir
      fs::create_directories(option_path, ec);
      if (ec) {
        LOG(kError) << "GetPathFromProgramOption - Unable to create new dir " << option_path << " ("
                    << ec.message() << ")";
        return fs::path();
      }
    } else {  // Create new file
      if (option_path.has_filename()) {
        try {
          std::ofstream ofs(option_path.c_str());
        }
        catch(const std::exception &e) {
          LOG(kError) << "GetPathFromProgramOption - Exception while creating new file: "
                      << e.what();
          return fs::path();
        }
      }
    }
  }

  if (is_dir) {
    if (!fs::is_directory(option_path, ec) || ec) {
      LOG(kError) << "GetPathFromProgramOption - Invalid " << option_name << ", " << option_path
                  << " is not a directory (" << ec.message() << ")";
      return fs::path();
    }
  } else {
    if (!fs::is_regular_file(option_path, ec) || ec) {
      LOG(kError) << "GetPathFromProgramOption - Invalid " << option_name << ", " << option_path
                  << " is not a regular file (" << ec.message() << ")";
      return fs::path();
    }
  }

  LOG(kInfo) << "GetPathFromProgramOption - " << option_name << " is " << option_path;
  return option_path;
}


void DoOnPublicKeyRequested(const NodeId& node_id,
                            const routing::GivePublicKeyFunctor& give_key,
                            nfs::PublicKeyGetter& public_key_getter) {
  public_key_getter.GetKey<passport::PublicPmid>(
      passport::PublicPmid::name_type(Identity(node_id.string())),
      [give_key, node_id] (std::string response) {
        nfs::Reply reply((nfs::Reply::serialised_type(NonEmptyString(response))));
        if (reply.IsSuccess()) {
          try {
            asymm::PublicKey public_key(
                asymm::DecodeKey(asymm::EncodedPublicKey(reply.data().string())));
            give_key(public_key);
          }
          catch(const std::exception& ex) {
            LOG(kError) << "Failed to get key for " << DebugId(node_id) << " : " << ex.what();
          }
        }
      });
}

bool SetupNetwork(const PmidVector &all_pmids, bool bootstrap_only) {
  assert(all_pmids.size() >= 2);

  struct BootstrapData {
    BootstrapData()
        : routing1(),
          routing2(),
          info1(),
          info2() {}
    std::shared_ptr<routing::Routing> routing1, routing2;
    routing::NodeInfo info1, info2;
  } bootstrap_data;

  auto make_node_info = [&] (const passport::Pmid& pmid)->routing::NodeInfo {
    routing::NodeInfo node;
    node.node_id = NodeId(pmid.name().data.string());
    node.public_key = pmid.public_key();
    return node;
  };

  LOG(kInfo) << "Creating zero state routing network...";
  bootstrap_data.info1 = make_node_info(all_pmids[0]);
  bootstrap_data.routing1.reset(new routing::Routing(&(all_pmids[0])));
  bootstrap_data.info2 = make_node_info(all_pmids[1]);
  bootstrap_data.routing2.reset(new routing::Routing(&(all_pmids[1])));

  std::vector<passport::PublicPmid> all_public_pmids;
  all_public_pmids.reserve(all_pmids.size());
  for (auto& pmid : all_pmids)
    all_public_pmids.push_back(passport::PublicPmid(pmid));
  nfs::PublicKeyGetter public_key_getter(*bootstrap_data.routing1, all_public_pmids);

  routing::Functors functors1, functors2;
  functors1.request_public_key = functors2.request_public_key =
      [&public_key_getter] (NodeId node_id,
                            const routing::GivePublicKeyFunctor& give_key) {
        DoOnPublicKeyRequested(node_id, give_key, public_key_getter);
      };

  UdpEndpoint endpoint1(GetLocalIp(), test::GetRandomPort());
  UdpEndpoint endpoint2(GetLocalIp(), test::GetRandomPort());
  auto a1 = std::async(std::launch::async, [&] {
    return bootstrap_data.routing1->ZeroStateJoin(functors1,
                                                  endpoint1,
                                                  endpoint2,
                                                  bootstrap_data.info2);
  });
  auto a2 = std::async(std::launch::async, [&] {
    return bootstrap_data.routing2->ZeroStateJoin(functors2,
                                                  endpoint2,
                                                  endpoint1,
                                                  bootstrap_data.info1);
  });
  if (a1.get() != 0 || a2.get() != 0) {
    LOG(kError) << "SetupNetwork - Could not start bootstrap nodes.";
    return false;
  }

  if (bootstrap_only) {
    // just wait till process receives termination signal
    std::cout << "Bootstrap nodes are running, press Ctrl+C to terminate." << std::endl
              << "Endpoints: " << endpoint1 << " and "
              << endpoint2 << std::endl;
    signal(SIGINT, CtrlCHandler);
    std::unique_lock<std::mutex> lock(mutex_);
    cond_var_.wait(lock, [] { return ctrlc_pressed; });  // NOLINT
    std::cout << "Shutting down bootstrap nodes." << std::endl;
    return true;
  }

//   priv::lifestuff_manager::ClientController client_controller(
//       [](const NonEmptyString&) {});
//   for (size_t i = 2; i < all_pmids.size(); ++i) {
//     if (client_controller.StartVault(all_pmids[i], all_pmids[i].name().first, "")) {
//       LOG(kSuccess) << "SetupNetwork - Started vault "
//                     << HexSubstr(all_pmids[i].name().first);
//     } else {
//       LOG(kError) << "SetupNetwork - Could not start vault "
//                   << HexSubstr(all_pmids[i].name().first);
//       return false;
//     }
//   }
//
//   Sleep(boost::posix_time::seconds(1));  // to keep bootstrap nodes alive for a bit

  return true;
}

std::future<bool> RoutingJoin(routing::Routing& routing,
                              routing::Functors& functors,
                              const std::vector<UdpEndpoint>& peer_endpoints) {
  std::once_flag join_promise_set_flag;
  std::promise<bool> join_promise;
  functors.network_status = [&join_promise_set_flag, &join_promise] (int result) {
                              std::call_once(join_promise_set_flag,
                              [&join_promise, &result] { join_promise.set_value(result == 0); });  // NOLINT (Fraser)
                            };
  // To allow bootstrapping off vaults on local machine
  routing::Parameters::append_local_live_port_endpoint = true;
  routing.Join(functors, peer_endpoints);
  return std::move(join_promise.get_future());
}

bool StoreKeys(const PmidVector& all_pmids, const std::vector<UdpEndpoint>& peer_endpoints) {
  passport::Anmaid client_anmaid;
  passport::Maid client_maid(client_anmaid);
  passport::Pmid client_pmid(client_maid);
  routing::Routing client_routing(nullptr);
  routing::Functors functors;

  if (!RoutingJoin(client_routing, functors, peer_endpoints).get()) {
    std::cout << "Failed to bootstrap anonymous node for storing keys";
    return false;
  }
  LOG(kInfo) << "Bootstrapped anonymous node to store keys";

  nfs::ClientMaidNfs client_nfs(client_routing, client_maid);

  auto get_callback = [] (std::promise<bool>& promise) -> routing::ResponseFunctor {
                        return [&promise] (std::string response) {
                                 try {
                                   nfs::Reply reply(
                                       (nfs::Reply::serialised_type(NonEmptyString(response))));
                                   promise.set_value(reply.IsSuccess());
                                 }
                                 catch(...) {
                                   promise.set_exception(std::current_exception());
                                 }
                               };
                      };
  auto store_keys = [&client_nfs, client_pmid, &get_callback] (const passport::Pmid& pmid,
                                                               std::promise<bool>& promise) {
                      passport::PublicPmid p_pmid(pmid);
                      client_nfs.Put(p_pmid, client_pmid.name(), get_callback(std::ref(promise)));
                    };

  std::vector<std::promise<bool> > bool_promises(all_pmids.size());
  std::vector<std::future<bool> > bool_futures;
  size_t promises_index(0);
  for (auto &pmid : all_pmids) {  // store all PMIDs
    std::async(std::launch::async,
               [&store_keys, pmid, &bool_promises, &promises_index] () {
                 store_keys(pmid, bool_promises.at(promises_index));
               });
    bool_futures.push_back(bool_promises.at(promises_index).get_future());
  }

  size_t error_stored_keys(0);
  for (auto& future : bool_futures) {
    if (future.has_exception() || !future.get())
      ++error_stored_keys;
  }

  if (error_stored_keys > 0) {
    std::cout << "StoreKeys - Could not store " << error_stored_keys << " out of "
                << all_pmids.size() << " keys.";
    return false;
  }
  LOG(kSuccess) << "StoreKeys - Stored all " << all_pmids.size() << " keys.";
  return true;
}

bool VerifyKeys(const PmidVector& all_pmids, const std::vector<UdpEndpoint>& peer_endpoints) {
  routing::Routing client_routing(nullptr);
  routing::Functors functors;

  if (!RoutingJoin(client_routing, functors, peer_endpoints).get()) {
    LOG(kError) << "Failed to bootstrap anonymous node to verify keys";
    return false;
  }
  LOG(kInfo) << "Bootstrapped anonymous node to verify keys";

  nfs::KeyGetterNfs key_getter_nfs(client_routing);

  auto verify_keys = [&key_getter_nfs] (const passport::Pmid& pmid,
                                        std::promise<bool>& promise) {
                       passport::PublicPmid p_pmid(pmid);
                       key_getter_nfs.Get<passport::PublicPmid>(
                           p_pmid.name(),
                           [&promise, &pmid] (std::string response) {
                             NonEmptyString nes_response(response);
                             nfs::Reply reply((nfs::Reply::serialised_type(nes_response)));
                             if (reply.IsSuccess()) {
                               try {
                                 asymm::PublicKey public_key(
                                     asymm::DecodeKey(
                                         asymm::EncodedPublicKey(reply.data().string())));
                                 promise.set_value(asymm::MatchingKeys(public_key,
                                                                       pmid.public_key()));
                               }
                               catch(...) {
                                 LOG(kError) << "Failed to get key for " << DebugId(pmid.name());
                                 promise.set_exception(std::current_exception());
                               }
                             }
                           });
                       };

  std::vector<std::future<bool> > futures;
  std::vector<std::promise<bool> > promises(all_pmids.size());
  size_t promises_index(0);
  for (auto& pmid : all_pmids) {
    std::async(std::launch::async, verify_keys, pmid, std::ref(promises.at(promises_index)));
    futures.push_back(promises.at(promises_index).get_future());
    ++promises_index;
  }

  size_t verified_keys(0);
  for (auto &future : futures) {
    if (!future.has_exception() && future.get())
      ++verified_keys;
  }

  if (verified_keys < all_pmids.size()) {
    LOG(kError) << "VerifyKeys - Could only verify " << verified_keys << " out of "
                << all_pmids.size() << " keys.";
    return false;
  }
  LOG(kSuccess) << "VerifyKeys - Verified all " << verified_keys << " keys.";
  return true;
}

bool StoreChunks(const PmidVector& all_pmids, const std::vector<UdpEndpoint>& peer_endpoints) {
  assert(all_pmids.size() >= 4);

  passport::Anmaid client_anmaid_1, client_anmaid_2;
  passport::Maid client_maid_1(client_anmaid_1), client_maid_2(client_anmaid_2);
  passport::Pmid client_pmid_1(client_maid_1);
  routing::Routing client_routing_1(nullptr), client_routing_2(nullptr);
  routing::Functors functors_1, functors_2;

  if (!RoutingJoin(client_routing_1, functors_1, peer_endpoints).get()) {
    LOG(kError) << "Failed to bootstrap anonymous node for storing chunks";
    return false;
  }
  if (!RoutingJoin(client_routing_2, functors_2, peer_endpoints).get()) {
    LOG(kError) << "Failed to bootstrap anonymous node for fetching chunks";
    return false;
  }
  LOG(kInfo) << "Bootstrapped anonymous node to store and fetch chunks";
  nfs::ClientMaidNfs client_nfs_1(client_routing_1, client_maid_1),
                               client_nfs_2(client_routing_2, client_maid_2);

  std::cout << "Going to store chunks, press Ctrl+C to stop." << std::endl;
  signal(SIGINT, CtrlCHandler);
  size_t num_chunks(0), num_store(0), num_get(0);

  std::unique_lock<std::mutex> lock(mutex_);
  while(!cond_var_.wait_for(lock, std::chrono::seconds(3), [] { return ctrlc_pressed; })) {  // NOLINT
    ImmutableData::serialised_type content(NonEmptyString(RandomString(1 << 18)));  // 256 KB
    ImmutableData::name_type name(Identity(crypto::Hash<crypto::SHA512>(content.data)));
    ImmutableData chunk_data(name, content);
    std::promise<bool> store_promise;
    std::future<bool> store_future(store_promise.get_future());
    routing::ResponseFunctor cb([&store_promise] (std::string response) {
                                            try {
                                              nfs::Reply reply(
                                                  (nfs::Reply::serialised_type(
                                                      NonEmptyString(response))));
                                              store_promise.set_value(reply.IsSuccess());
                                            }
                                            catch(...) {
                                              store_promise.set_exception(std::current_exception());
                                            }
                                          });
    std::cout << "Storing chunk " << HexSubstr(name.data.string()) << " ..." << std::endl;
    client_nfs_1.Put(chunk_data, client_pmid_1.name(), cb);
    ++num_chunks;

    if (store_future.get()) {
      std::cout << "Stored chunk " << HexSubstr(name.data.string()) << std::endl;
      ++num_store;
    } else {
      std::cout << "Failed to store chunk " << HexSubstr(name.data.string())
                << std::endl;
      continue;
    }
    // The current client is anonymous, which incurs a 10 mins faded out for stored data
    std::cout << "Going to retrieve the stored chunk" << std::endl;
    std::promise<bool> get_promise;
    std::future<bool> get_future(get_promise.get_future());
    auto equal_immutables = [] (const ImmutableData& lhs, const ImmutableData& rhs) {
                              return lhs.name().data.string() == lhs.name().data.string() &&
                                     lhs.data().string() == rhs.data().string();
                            };

    cb = [&chunk_data, &get_promise, &equal_immutables] (std::string response) {
           try {
             nfs::Reply reply((nfs::Reply::serialised_type(NonEmptyString(response))));
             ImmutableData data(reply.data());
             get_promise.set_value(equal_immutables(chunk_data, data));
           }
           catch(...) {
             get_promise.set_exception(std::current_exception());
           }
         };
    client_nfs_2.Get<ImmutableData>(name, cb);
    if (get_future.get()) {
      std::cout << "Got chunk " << HexSubstr(name.data.string()) << std::endl;
      ++num_get;
    } else {
      std::cout << "Failed to store chunk " << HexSubstr(name.data.string()) << std::endl;
    }
  }

  return num_get == num_chunks;
}

// bool ExtendedTest(const size_t& chunk_set_count,
//                   const FobPairVector& all_keys,
//                   const std::vector<UdpEndpoint>& peer_endpoints) {
//   const size_t kNumClients(3);
//   assert(all_keys.size() >= 2 + kNumClients);
//   std::vector<std::unique_ptr<pd::Node>> clients;
//   std::vector<test::TestPath> client_paths;
//   std::vector<std::unique_ptr<pcs::RemoteChunkStore>> rcs;
//   for (size_t i = 2; i < 2 + kNumClients; ++i) {
//     std::cout << "Setting up client " << (i - 1) << " ..." << std::endl;
//     std::unique_ptr<pd::Node> client(new pd::Node);
//     client_paths.push_back(test::CreateTestPath("MaidSafe_Test_KeysHelper"));
//     client->set_account_name(all_keys[i].first.identity);
//     client->set_fob(all_keys[i].first);
//     if (client->Start(*client_paths.back(), peer_endpoints) != 0) {
//       std::cout << "Failed to start client " << (i - 1) << std::endl;
//       return false;
//     }
//     rcs.push_back(std::move(std::unique_ptr<pcs::RemoteChunkStore>(new pcs::RemoteChunkStore(
//         client->chunk_store(), client->chunk_manager(), client->chunk_action_authority()))));
//     clients.push_back(std::move(client));
//   }
//
//   const size_t kNumChunks(3 * chunk_set_count);
//   std::atomic<size_t> total_ops(0), succeeded_ops(0);
//   typedef std::map<pd::ChunkName,
//                    std::pair<NonEmptyString, Fob>> ChunkMap;
//   ChunkMap chunks;
//   for (size_t i = 0; i < kNumChunks; ++i) {
//     pd::ChunkName chunk_name;
//     NonEmptyString chunk_contents;
//     auto chunk_fob = clients[0]->fob();
//     switch (i % 3) {
//       case 0:  // DEF
//         chunk_contents = NonEmptyString(RandomString(1 << 18));  // 256 KB
//         chunk_name = priv::ApplyTypeToName(
//             crypto::Hash<crypto::SHA512>(chunk_contents),
//             priv::ChunkType::kDefault);
//         break;
//       case 1:  // MBO
//         pd::CreateSignedDataPayload(
//             NonEmptyString(RandomString(1 << 16)),  // 64 KB
//             chunk_fob.keys.private_key,
//             chunk_contents);
//         chunk_name = priv::ApplyTypeToName(
//                          NodeId(NodeId::kRandomId),
//                          priv::ChunkType::kModifiableByOwner);
//         break;
//       case 2:  // SIG
//         chunk_fob = pd::GenerateIdentity();
//         pd::CreateSignaturePacket(chunk_fob, chunk_name, chunk_contents);
//         break;
//     }
//     chunks[chunk_name] = std::make_pair(chunk_contents, chunk_fob);
//     LOG(kInfo) << "ExtendedTest - Generated chunk "
//                << pd::DebugChunkName(chunk_name) << " of size "
//                << BytesToBinarySiUnits(chunk_contents.string().size());
//   }
//
//   enum class RcsOp {
//     kStore = 0,
//     kModify,
//     kDelete,
//     kGet
//   };
//
//   auto do_rcs_op = [&rcs, &total_ops, &succeeded_ops](RcsOp rcs_op,
//                                                       ChunkMap::value_type& chunk,
//                                                       bool expect_success) {
//     std::mutex mutex;
//     std::condition_variable cond_var;
//     std::atomic<bool> cb_done(false), cb_result(false);
//     auto cb = [&cond_var, &cb_done, &cb_result](bool result) {
//       cb_result = result;
//       cb_done = true;
//       cond_var.notify_one();
//     };
//     bool result(false);
//     std::string action_name;
//     switch (rcs_op) {
//       case RcsOp::kStore:
//         action_name = "Storing";
//         result = rcs[0]->Store(chunk.first, chunk.second.first, cb, chunk.second.second);
//         break;
//       case RcsOp::kModify:
//         action_name = "Modifying";
//         {
//           NonEmptyString new_content;
//           pd::CreateSignedDataPayload(
//               NonEmptyString(RandomString(1 << 16)),  // 64 KB
//               chunk.second.second.keys.private_key,
//               new_content);
//           if (expect_success)
//             chunk.second.first = new_content;
//           result = rcs[0]->Modify(chunk.first, chunk.second.first, cb, chunk.second.second);
//         }
//         break;
//       case RcsOp::kDelete:
//         action_name = "Deleting";
//         result = rcs[0]->Delete(chunk.first, cb, chunk.second.second);
//         break;
//       case RcsOp::kGet:
//         action_name = "Getting";
//         result = (chunk.second.first.string() == rcs[1]->Get(chunk.first, chunk.second.second,
//                                                              false));
//         break;
//     }
//     if (rcs_op != RcsOp::kGet && result) {
//       std::unique_lock<std::mutex> lock(mutex);
//       cond_var.wait(lock, [&cb_done] { return cb_done.load(); });  // NOLINT
//       result = cb_result;
//     }
//     ++total_ops;
//     if (result == expect_success) {
//       ++succeeded_ops;
//       LOG(kSuccess) << "ExtendedTest - " << action_name << " "
//                     << pd::DebugChunkName(chunk.first) << " "
//                     << (result ? "succeeded" : "failed") << " as expected.";
//     } else {
//       LOG(kError) << "ExtendedTest - " << action_name << " "
//                   << pd::DebugChunkName(chunk.first) << " "
//                   << (result ? "succeeded" : "failed") << " unexpectedly.";
//     }
//   };
//
//   size_t prev_ops_diff(0);
//   auto const kStageDescriptions = []()->std::vector<std::string> {
//     std::vector<std::string> desc;
//     desc.push_back("storing chunks");
//     desc.push_back("retrieving chunks");
//     desc.push_back("modifying chunks");
//     desc.push_back("retrieving modified chunks");
//     desc.push_back("deleting chunks");
//     desc.push_back("retrieving deleted chunks");
//     desc.push_back("modifying non-existing chunks");
//     desc.push_back("re-storing chunks");
//     desc.push_back("retrieving chunks");
//     desc.push_back("deleting chunks");
//     return desc;
//   }();
//   for (int stage = 0; stage < 9; ++stage) {
//     std::cout << "Processing test stage " << (stage + 1) << ": " << kStageDescriptions[stage]
//               << "..." << std::endl;
//     for (auto& rcs_it : rcs)
//       rcs_it->Clear();
//     auto start_time = boost::posix_time::microsec_clock::local_time();
//     for (auto& chunk : chunks) {
//       auto chunk_type = priv::GetChunkType(chunk.first);
//       switch (stage) {
//         case 0:  // store all chunks
//           do_rcs_op(RcsOp::kStore, chunk, true);
//           break;
//         case 1:  // get all chunks to verify storage
//           do_rcs_op(RcsOp::kGet, chunk, true);
//           break;
//         case 2:  // modify all chunks, should only succeed for MBO
//           do_rcs_op(RcsOp::kModify, chunk,
//                     chunk_type == priv::ChunkType::kModifiableByOwner);
//           break;
//         case 3:  // get all chunks to verify (non-)modification
//           do_rcs_op(RcsOp::kGet, chunk, true);
//           break;
//         case 4:  // delete all chunks
//           do_rcs_op(RcsOp::kDelete, chunk, true);
//           break;
//         case 5:  // get all chunks to verify deletion
//           do_rcs_op(RcsOp::kGet, chunk, false);
//           break;
//         case 6:  // modify all (non-existing) chunks
//           do_rcs_op(RcsOp::kModify, chunk, false);
//           break;
//         case 7:  // store all chunks again, only SIG should fail due to revokation
//           do_rcs_op(RcsOp::kStore, chunk,
//                     chunk_type != priv::ChunkType::kSignaturePacket);
//           break;
//         case 8:  // get all chunks again, only SIG should fail due to revokation
//           do_rcs_op(RcsOp::kGet,
//                     chunk,
//                     chunk_type != priv::ChunkType::kSignaturePacket);
//           break;
//         case 9:  // delete all chunks to clean up
//           do_rcs_op(RcsOp::kDelete, chunk, true);
//           break;
//         default:
//           break;
//       }
//     }
//     for (auto& rcs_it : rcs)
//       rcs_it->WaitForCompletion();
//     auto end_time = boost::posix_time::microsec_clock::local_time();
//     size_t ops_diff = total_ops - succeeded_ops;
//     std::cout << "Stage " << (stage + 1) << ": " << kStageDescriptions[stage] << " - "
//               << (ops_diff == prev_ops_diff ? "SUCCEEDED" : "FAILED") << " ("
//               << (end_time - start_time) << ")" << std::endl;
//     prev_ops_diff = ops_diff;
//   }
//
//   for (auto& client : clients)
//     client->Stop();
//
//   std::cout << "Extended test completed " << succeeded_ops << " of " << total_ops
//             << " operations for " << kNumChunks << " chunks successfully." << std::endl;
//   return succeeded_ops == total_ops;
// }

}  // namespace tools

}  // namespace vault

}  // namespace maidsafe
