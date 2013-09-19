/*  Copyright 2013 MaidSafe.net limited

    This MaidSafe Software is licensed to you under (1) the MaidSafe.net Commercial License,
    version 1.0 or later, or (2) The General Public License (GPL), version 3, depending on which
    licence you accepted on initial access to the Software (the "Licences").

    By contributing code to the MaidSafe Software, or to this project generally, you agree to be
    bound by the terms of the MaidSafe Contributor Agreement, version 1.0, found in the root
    directory of this project at LICENSE, COPYING and CONTRIBUTOR respectively and also
    available at: http://www.maidsafe.net/licenses

    Unless required by applicable law or agreed to in writing, the MaidSafe Software distributed
    under the GPL Licence is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS
    OF ANY KIND, either express or implied.

    See the Licences for the specific language governing permissions and limitations relating to
    use of the MaidSafe Software.                                                                 */

#include "maidsafe/vault/maid_manager/action_delete.h"

#include "maidsafe/vault/maid_manager/metadata.h"
#include "maidsafe/vault/maid_manager/value.h"


namespace maidsafe {

namespace vault {

const nfs::MessageAction ActionMaidManagerDelete::kActionId(nfs::MessageAction::kDeleteRequest);

void ActionMaidManagerDelete::operator()(MaidManagerMetadata& metadata,
                                         boost::optional<MaidManagerValue>& value) const {
  if (!value)
    return;
  auto deleted_cost(value->Delete());
  if (value->count() == 0)
    value.reset();
  metadata.DeleteData(deleted_cost);
}

}  // namespace vault

}  // namespace maidsafe
