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

from google.appengine.ext import webapp
from google.appengine.ext.webapp.util import run_wsgi_app
from handler import client
from handler import datum
from handler import metric
from handler import product


class MainHandler(webapp.RequestHandler):
  def get(self):
    self.response.headers['Content-Type'] = 'text/plain'
    self.response.out.write('Syzygy Dashboard')


# Add debug=True to the app's arguments for debugging.
application = webapp.WSGIApplication(
    [(r'^/$', MainHandler),
     # /<product>
     (r'^/([^/]+)/?$', product.ProductHandler),
     # /<product>/<client>
     (r'^/([^/]+)/([^/]+)/?$', client.ClientHandler),
     # /<product>/<client>/<metric>
     (r'^/([^/]+)/([^/]+)/([^/]+)/?$', metric.MetricHandler),
     # /<product>/<client>/<metric>/data
     (r'^/([^/]+)/([^/]+)/([^/]+)/data/?$', datum.DatumHandler)],
     debug=True)


def main():
  run_wsgi_app(application)


if __name__ == "__main__":
  main()
