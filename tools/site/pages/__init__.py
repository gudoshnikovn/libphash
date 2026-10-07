"""One module per algorithm page of the site (docs/theory/<page>.md), in the order of the
site's navigation. tools/site/README.md describes what a module declares (ALGO, BITS or
METRIC and its companions, COST_VARIANTS, …) and what figures() draws."""
from pages import ahash, bmh, color_hash, color_moments, dhash, mhash, phash, radial, whash

PAGES = (ahash, dhash, phash, whash, mhash, bmh, radial, color_hash, color_moments)
