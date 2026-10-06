# Timox VASIO Control UX/UI review

[Français](UX_UI_REVIEW.md) | **English**

> Historical review of screenshots from before version 1.1.0. For the shipped interface, see the [1.1.0 feature guide](FONCTIONS_1.1.0.en.md).

## Scope

The reviewed workflow covered setting application profiles, selecting the physical ASIO device, building a route in the matrix, and checking or changing existing routes. The user supplied the five screenshots below on October 4, 2026. They show the application open at that time, with “VASIO Control” in its header and no visible Logs/Swagger tabs. They therefore do not show the candidate build rebuilt afterward.

## Steps and overall state

### 1. Channel profiles — sound, with overly strong visual hierarchy

Profiles are grouped in a card with explicit columns for executable, inputs, outputs, and action. A message confirms saving and says applications must be restarted. This helps explain when the change takes effect. However, the bright cyan title dominates the content and attracts more attention than engine status.

![Profiles and save status](ux-audit/evidence/01-profiles-top.png)

### 2. Physical device and clients — sound

Driver, sample rate, and buffer size form a coherent group. The empty state “No application connected to TimoxVasio” is explicit. Another screenshot shows Renoise connected with its published channels.

![Physical settings and start of the matrix](ux-audit/evidence/02-physical-settings-routing.png)

![Profiles and connected Renoise channels](ux-audit/evidence/03-profiles-and-clients.png)

### 3. Routing matrix — usable but dense

Source and destination areas, pagination, and searches are separated and labeled. The matrix's amber scrollbars stand out from the background. Full channel names are rotated vertically, making them slow and repetitive to read; much of the width remains empty when only 16 columns are shown. The screenshot selects physical inputs to virtual inputs, while the configuration visible elsewhere contains routes from virtual to physical outputs. The static “Configured route” legend suggests a route exists in that particular matrix.

![Routing matrix with filters and pagination](ux-audit/evidence/04-routing-matrix.png)

The legend has since been corrected: it shows the number of routes on the displayed page or “No routes in this view.” The supplied screenshots predate that change.

### 4. Route list — visible controls, overly technical labels

“Show in matrix,” gain, mute, and delete controls are easy to find. However, routes are presented with repeated technical identifiers (`virtual:TimoxVasio:<PID>:output:<channel>`), making a long list hard to scan. A later improvement should provide readable endpoint names from the API and retain the technical identifier in the details.

![Route list and controls](ux-audit/evidence/05-configured-routes.png)

## Colors, text, surfaces, and accessibility

- Body text and controls remain readable on dark surfaces. Saturated cyan accents and the title glow are too prominent.
- Sections are distinct, but old CSS reactivated bright cyan on section headings and scrollbars after the new tokens. These cascade rules were corrected: softer accent, 20 px section heading, neutral general scrollbars without a glow, and a font stack inherited from one place.
- Base text is 16 px; controls are at least 40 px and helper text is 14 px. Card and control outlines are visible.
- The code uses row and column headers and cell buttons with accessible labels. Screenshots alone cannot validate keyboard navigation, screen readers, computed contrast, or zoom.

## Scope and remaining checks

- The five screenshots describe the interface supplied on October 4, not version 1.0.0 reinstalled on October 5. CSS corrections are included in the published version, but their appearance was not recaptured in this review.
- Logs and API/Swagger views do not appear in the audit screenshots. The user subsequently verified Swagger in the installed application; this confirms access and operation, not its detailed visual presentation.
- Full keyboard navigation, focus on every control, resizing, and screen reader reading of the matrix were not checked separately.

This review is based on five screenshots and inspection of React/CSS sources, supplemented by confirmation of Swagger use on October 5. It is not a WCAG conformance statement. These limits describe the accessibility audit's scope; they do not call into question the audio, logs, or Swagger validation for version 1.0.0.
