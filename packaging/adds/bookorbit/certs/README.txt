ca-bundle.pem: the CA certificates Smalt uses for HTTPS to BookOrbit.

Source: curl's cacert.pem (https://curl.se/docs/caextract.html), extracted
from Mozilla's root certificate store. The certificate data is made
available by Mozilla under the Mozilla Public License, version 2.0
(https://www.mozilla.org/MPL/2.0/). The date of the Mozilla data is in the
file's header.

Smalt ships its own bundle because the Kobo's built-in certificates may be
too old for current Let's Encrypt chains. To refresh it, run
scripts/update-ca-bundle.sh in the Smalt repository.
