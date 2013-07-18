/* Copyright 2013 MaidSafe.net limited

This MaidSafe Software is licensed under the MaidSafe.net Commercial License, version 1.0 or later,
and The General Public License (GPL), version 3. By contributing code to this project You agree to
the terms laid out in the MaidSafe Contributor Agreement, version 1.0, found in the root directory
of this project at LICENSE, COPYING and CONTRIBUTOR respectively and also available at:

http://www.novinet.com/license

Unless required by applicable law or agreed to in writing, software distributed under the License is
distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
implied. See the License for the specific language governing permissions and limitations under the
License.
*/

#ifndef MAIDSAFE_VAULT_DATA_MANAGER_DATA_MANAGER_H_
#define MAIDSAFE_VAULT_DATA_MANAGER_DATA_MANAGER_H_

#include <cstdint>
#include <utility>

#include "maidsafe/common/types.h"
#include "maidsafe/nfs/types.h"
#include "maidsafe/vault/key.h"
#include "maidsafe/vault/data_manager/value.h"
#include "maidsafe/vault/unresolved_element.h"

namespace maidsafe {

namespace nfs {

template<>
struct PersonaTypes<Persona::kDataManager> {
  static const Persona persona = Persona::kDataManager;
  static const int kPaddedWidth = 1;
};

}  // namespace nfs

namespace vault {


typedef nfs::PersonaTypes<nfs::Persona::kDataManager> DataManager;
typedef std::pair<Key<DataManager>, nfs::MessageAction> UnresolvedEntryKey;
typedef vault::DataManagerValue UnresolvedEntryValue;
typedef UnresolvedElement<DataManager> DataManagerUnresolvedEntry;
typedef DataManagerUnresolvedEntry DataManagerResolvedEntry;
}  // namespace vault

}  // namespace maidsafe

#endif  // MAIDSAFE_VAULT_DATA_MANAGER_DATA_MANAGER_H_