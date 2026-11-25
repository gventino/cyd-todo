# Findings for cydui

Everything in the library that got in the way while writing this application, one entry each, with what happened, what was done in the application, and what the library should do (fix, document, or leave). Closing this file out is the condition for `cydui` 1.0.0.

| # | Where | What happened | Application workaround | Library action | State |
|---|---|---|---|---|---|
| 1 | `cydui_draw_bitmap` and dirty regions | The hash of a strip compares the bitmap **pointer**, not its pixels, so a QR drawn into the same buffer with new content is not redrawn. | A new buffer per QR, or `cydui_invalidate`. | Document it in `docs/12-guide.md` (already in the header of `dirty_mem`; add the QR case). | open |

*(Entries are added as they are found, with the date and the step.)*
