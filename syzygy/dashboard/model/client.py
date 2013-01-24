# Copyright 2011 Google Inc. All Rights Reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

from google.appengine.ext import db

class Client(db.Model):
  # Key: The name of the client should be stored by the app in the entity's
  # key_name. This will be used to uniquely identify the client instead of
  # having appengine assign an integer ID.

  # Parent: The parent of a Client entity is a Product.

  # A long form description of this client.
  description = db.TextProperty(required=True)
