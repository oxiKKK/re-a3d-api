# Maintaining documentation

User instructions belong in the user guides, application workflows in the
programming guides, and system architecture in internals. Declarations and
function contracts belong in the owning code.

## Authorities

| Subject | Authority |
| --- | --- |
| Build defaults and dependencies | CMake files |
| Public declarations and identifiers | `inc/ia3dapi.h`, `inc/ia3dutil.h`, and applicable Windows headers |
| Current behavior | Implementation and meaningful tests |
| Original behavior | Matching reference binaries and qualified evidence |
| Tool arguments and artifacts | Tool source/`--help` and directory README |
| Recorded test results | Reproducible run with configuration and DLL identity |

Do not copy a historical support claim into a modern-system guide without
checking its scope. External platform behavior should link to a primary source
near the claim. Unsupported environments should be identified as unverified.

## Documentation and code

Keep the manual at the system and workflow level. Link to the owning headers
and implementations for argument names, types, units, ownership, validation,
and return contracts. Do not generate or copy interface slot tables, method
signatures, member lists, structure definitions, or constant/error inventories
into documentation.

When reviewing header declarations, check parameter names against the
definitions. Check existing comments against the banner rules,
including every overload defined inside classes and structs.
Repository-owned source headings use the project identification and archival
notice specified by those banner rules; preserved SDK, sample and vendored
source retains its original heading.
Source comments describe enduring behavior and constraints; do not label them
with project phases, milestones, roadmaps or reconstruction progress.

Before removing an explanation from the manual, check that any essential
contract is recorded beside the relevant declaration or implementation.
Preserve historical SDK headers and sample text; project-specific contracts
belong in project-owned source comments. Do not copy whole documentation
paragraphs into comments when a brief contract is sufficient.

Retain diagrams that explain relationships, examples that demonstrate a
workflow, and commands/settings needed to build, run, or diagnose the system.
Compiler layout reports and binary comparisons serve reconstruction validation;
they are separate from the manual's API guidance.

```powershell
python tools\documentation\check_links.py
```

The checker includes project README pages, dependency guidance and repository
skills as well as `docs/`. When removing a feature or build option, audit every
documentation page for its identifiers and indirect behavior descriptions.
Retain old names only in migration guidance or explicitly historical records.

## Existing reconstruction reports

The class/file/address/layout report generators write to `docs/llm/`, alongside
their evidence tables in `docs/llm/groundtruth/`. Their commands and inputs are
listed in [tools/README.md](../../tools/README.md). Layout regeneration invokes
MSVC and is more expensive than a Markdown check.

The extracted SDK documents preserve historical text and generator notices.
They serve as historical evidence inputs.
Regenerate them through the extractor if their source format changes.

## Mermaid diagrams

Keep diagram sources in fenced `mermaid` blocks beside the text they explain.
GitHub renders these blocks directly; see its
[diagram documentation](https://docs.github.com/en/get-started/writing-on-github/working-with-advanced-formatting/creating-diagrams).
Use short labels, ordinary flowcharts or sequences, and an `accTitle` and
`accDescr` for accessibility. Describe meaningful omissions near the diagram,
especially conditional effects and asynchronous work. Avoid copying method
inventories into diagrams.

[render_diagrams.mjs](../../tools/documentation/render_diagrams.mjs) discovers
all Mermaid fences under `docs/` and renders each in default and dark themes.
It uses the local [Mermaid renderer](https://mermaid.js.org/config/usage.html)
and an installed Chromium browser. It writes SVG, PNG, extracted Mermaid,
a source-hash manifest, and an HTML gallery under ignored `artifacts/`.
The renderer serves assets on loopback and blocks external page requests.

Install Node.js 20 or newer and Chrome, Edge, or another Chromium browser.
From the repository root, install the pinned rendering dependencies once:

```powershell
npm install --prefix artifacts/documentation/mermaid-runtime --ignore-scripts --no-audit --no-fund mermaid@11.12.0 puppeteer-core@24.22.3
```

Render and open the gallery, adjusting the browser path for your installation:

```powershell
node tools/documentation/render_diagrams.mjs --browser "C:\Program Files\Google\Chrome\Application\chrome.exe"
Start-Process artifacts/documentation/mermaid/index.html
```

`CHROME_PATH` can supply the browser path instead. `--runtime` selects another
dependency directory; `--output` selects another preview directory. The manifest
records the actual Mermaid version, source locations, hashes, dimensions, and
scale. A parser error, geometry outside the SVG bounds, or scaling below 75%
causes failure. These checks do not detect every overlapping or overflowing
label: inspect every rendered diagram for reading order, arrow endpoints,
label fit, and contrast in both themes. Correct the Markdown and render again.

The local renderer does not reproduce GitHub's exact theme or guarantee its
current Mermaid version. Keep syntax conservative and inspect the GitHub view
when publishing. Preview images are generated review artifacts; the Markdown
fences remain the documentation source.

## Review checklist

- Verify option defaults and every named command/target against current source.
- Check relative files and heading fragments after moving pages.
- Compile complete code examples using the documented architecture.
- Label pseudocode or partial sequences and state their error-handling assumptions.
- Keep original behavior, project additions, test observations, and unknowns distinct.
- Render and visually inspect Mermaid diagrams after changing their source.
- Avoid duplicating run logs, source maps, or obsolete plans in prose pages.

Formatting reviews must preserve mixed line endings as well as the prevailing
line ending. Compare edited files with a fresh working-tree snapshot so existing
user changes remain part of the baseline.

For a documentation-only change, check links and inspect the affected prose.
Compile examples when their code changes, render diagrams when their source
changes, and check affected generated evidence reports. Run implementation
tests when the change also affects runtime code or test infrastructure.
