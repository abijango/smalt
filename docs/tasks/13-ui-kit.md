# 13: UI kit

**Wave 1.**

## Goal

The design system as Qt 5 widgets: tokens, palettes, layout in millimetres, and every component the screens need.

## Spec sections

Visual design (all), UX details and copy (E Ink rules), Large-library UX. Mockups in `docs/mockups/`.

## Owns

`src/ui/kit/`, `tests/ui/kit/`, `tools/ui-preview/`.

## Must not touch

`src/ui/screens/` (task 22), `include/smalt/` (propose changes).

## Build

- Tokens exactly as the spec's colour table. Two palettes: colour and monochrome (Clara BW), chosen from the device model at startup.
- Layout unit in millimetres converted with the device's ppi (300 on all three targets), so sizes are physically identical across devices. Device profiles: Clara Colour, Clara BW, Libra Colour (resolutions in the spec).
- Components: header bar, status line, section header with count, cover (with format label and progress bar), cover row, list row (cover, title, subtitle, trailing badge), download badge, primary button (black), accent button (teal, download only), outline button, chip and chip row, bottom tabs (4 on Claras, 5 on Libra), pager with page counter and jump-to-letter, search field, dialog with 2 px border over a dimmed page, banner, segmented series progress bar, cover stack.
- E Ink behaviour: no animations; components request partial refresh for presses, full refresh for screen changes, through an interface the Nickel side implements later.
- Touch targets at least 9 mm tall.
- A desktop preview app (`tools/ui-preview/`) that renders each component and the three device frames at true pixel sizes, for screenshot comparison with `docs/mockups/`.

## Acceptance tests

- Palette swap replaces every teal use with the monochrome mapping.
- Millimetre conversion gives identical physical sizes on 6" and 7" profiles.
- Screenshot tests of each component in both palettes.
