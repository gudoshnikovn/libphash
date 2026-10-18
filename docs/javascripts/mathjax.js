// MathJax for the formulas pymdownx.arithmatex marks up (class "arithmatex"), loaded only
// on a page that has one: it is a quarter of a megabyte of script, and typesetting a page
// holds up the browser, so a page without a formula, most of the site, goes without it.
// The document$ observable fires on every page, including pages loaded by instant
// navigation, so a formula is typeset on whichever page it is reached from, and the
// script is loaded the first time one is.
//
// The version is pinned, as every other dependency of the site is: a release of MathJax
// changes how a formula renders. 3.2.2 is the last of version 3; version 4 changes the
// configuration below.
const MATHJAX = "https://cdn.jsdelivr.net/npm/mathjax@3.2.2/es5/tex-mml-chtml.js";

window.MathJax = {
  tex: {
    inlineMath: [["\\(", "\\)"]],
    displayMath: [["\\[", "\\]"]],
    processEscapes: true,
    processEnvironments: true,
  },
  options: {
    ignoreHtmlClass: ".*|",
    processHtmlClass: "arithmatex",
  },
  // The first page's formulas are typeset by the subscription below, as every later
  // page's are, not by the script on load.
  startup: { typeset: false },
};

let loading = null;

function load() {
  if (!loading) {
    loading = new Promise((resolve, reject) => {
      const script = document.createElement("script");
      script.src = MATHJAX;
      script.async = true;
      script.onload = () => MathJax.startup.promise.then(resolve, reject);
      script.onerror = reject;
      document.head.appendChild(script);
    });
  }
  return loading;
}

document$.subscribe(() => {
  if (!document.querySelector(".arithmatex")) {
    return;
  }
  load().then(() => {
    MathJax.startup.output.clearCache();
    MathJax.typesetClear();
    MathJax.texReset();
    return MathJax.typesetPromise();
  });
});
