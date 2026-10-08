"""One module per page of the site with figures (docs/theory/<page>.md): the algorithm
pages in the order of the site's navigation, then the topic pages. tools/site/README.md describes what a module declares (ALGO, BITS or
METRIC and its companions, COST_VARIANTS, …) and what figures() draws."""
from pages import ahash, bmh, color_hash, color_moments, dhash, mhash, phash, radial, whash

PAGES = (ahash, dhash, phash, whash, mhash, bmh, radial, color_hash, color_moments)

# Pages that explain something every algorithm shares and have no hash of their own: their
# module draws its figures from figures(tool, image, out_dir, timing) and nothing else.
from pages import preparation  # noqa: E402

TOPICS = (preparation,)
