#!python
# Copyright 2012 Google Inc.
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

import httplib
import json
from google.appengine.ext import webapp
from model import client as client_db
from model import product as product_db


class ProductHandler(webapp.RequestHandler):
  """A class to handle creating, reading, updating and deleting products.

  Handles GET, POST and DELETE requests for /products/ and /products/<product>.
  All functions have the same signature, even though they may not use all the
  parameters, so that a single route can be used for the handler. Note that PUT
  is not handled because a product has no extra information to update.
  """

  def get(self, product_id):
    """Responds with information about all products or a specific product.

    /products/
      Responds with a JSON encoded object that contains a list of product IDs.
    /products/<product>
      Responds with a JSON encoded object of the product ID and its child client
      IDs for the given product.

    Args:
      product_id. The product ID. May be empty.
    """
    if not product_id:
      products = product_db.Product.all()
      products_result = [{'product_id': p.key().name()} for p in products]
      result = {'products': products_result}
    else:
      product = product_db.Product.get_by_key_name(product_id)
      if not product:
        self.error(httplib.NOT_FOUND)
        return

      client_keys = client_db.Client.all(keys_only=True)
      client_keys.ancestor(product)
      client_ids = [key.name() for key in client_keys]

      result = {'product_id': product.key().name(),
                'client_ids': client_ids}

    self.response.headers['Content-Type'] = 'application/json'
    json.dump(result, self.response.out)

  def post(self, product_id):
    """Creates a new product.

    /products/
      Creates a new product. The product ID should be specified in the body of
      the request.
    /products/<product>
      Unused.

    Args:
      product_id: The product ID. Must be empty.
    """
    # Validate input.
    if product_id:
      self.error(httplib.BAD_REQUEST)
      return

    product_id = self.request.get('product_id')
    if not product_id:
      self.error(httplib.BAD_REQUEST)
      return

    # Perform DB lookups.
    # Make sure that this product ID does not already exist.
    if product_db.Product.get_by_key_name(product_id):
      self.error(httplib.BAD_REQUEST)
      return

    # Create a new product.
    product = product_db.Product(key_name=product_id)
    product.put()
    self.response.set_status(httplib.CREATED, message='ProductCreated')

  def delete(self, product_id):
    """Deletes a product.

    /products/
      Unused
    /products/<product>
      Deletes the specified product.

    Args:
      product_id: The product ID. Must not be empty.
    """
    # Validate input.
    if not product_id:
      self.error(httplib.BAD_REQUEST)
      return

    # Perform DB lookups.
    product = product_db.Product.get_by_key_name(product_id)
    if not product:
      self.error(httplib.NOT_FOUND)
      return
    
    # Delete the product.
    product.delete()
