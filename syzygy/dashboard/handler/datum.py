# Copyright 2011 Google Inc.
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

import json
from google.appengine.ext import webapp
from model import client as client_db
from model import datum as datum_db
from model import metric as metric_db
from model import product as product_db


class DatumHandler(webapp.RequestHandler):
  """A class to handle creating and querying data."""

  def get(self, product_id, client_id, metric_id):
    """Responds with information about data.

    Responds with a JSON encoded product ID, client ID, metric ID, product
    version, toolchain version, timestamp and values for a given product_id,
    client_id and metric_id triplet. If the product, client or metric doesn't
    exist, responds with a 404.

    Args:
      product_id: The product ID.
      client_id: The client ID.
      metric_id: The metric ID.
    """
    product = product_db.Product.get_by_key_name(product_id)
    if not product:
      self.error(404)  # Not found.
      return

    client = client_db.Client.get_by_key_name(client_id, product)
    if not client:
      self.error(404)  # Not found.
      return

    metric = metric_db.Metric.get_by_key_name(metric_id, client)
    if not metric:
      self.error(404)  # Not found.
      return

    data = datum_db.Datum.all()
    data.ancestor(metric)

    data_result = []
    for datum in data:
      data_result.append({'datum_id': datum.key().id(),
                          'product_version': datum.product_version,
                          'toolchain_version': datum.toolchain_version,
                          'timestamp': str(datum.timestamp),
                          'values': datum.values})

    result = {'product_id': product.key().name(),
              'client_id': client.key().name(),
              'metric_id': metric.key().name(),
              'data': data_result}

    self.response.headers['Content-Type'] = 'application/json'
    json.dump(result, self.response.out)

  def post(self, product_id, client_id, metric_id):
    """Creates a new datum.

    Adds a datum to the data store. The product, client and metric IDs should be
    specified in the URL and the product_version, toolchain_version and values
    should be specified as POST parameters in the request. Responds with a 200
    on success or a 400 if there are invalid parameters.

    Args:
      product_id: The product ID.
      client_id: The client ID.
      metric_id: The metric ID.
    """
    product_version = self.request.get('product_version', None)
    toolchain_version = self.request.get('toolchain_version', None)
    values = self.request.get_all('values', None)
    if not product_version or not toolchain_version or not values:
      self.error(400)  # Bad request.
      return

    try:
      values = [float(value) for value in values]
    except ValueError:
      self.error(400)  # Bad request.
      return

    product = product_db.Product.get_by_key_name(product_id)
    if not product:
      self.error(404)  # Not found.
      return

    client = client_db.Client.get_by_key_name(client_id, product)
    if not client:
      self.error(404)  # Not found.
      return

    metric = metric_db.Metric.get_by_key_name(metric_id, client)
    if not metric:
      self.error(404)  # Not found.
      return

    # Creates a new datum.
    datum = datum_db.Datum(parent=metric, product_version=product_version,
                           toolchain_version=toolchain_version, values=values)
    datum.put()
