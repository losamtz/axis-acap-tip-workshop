# AcapAngularUi

This project was generated using [Angular CLI](https://github.com/angular/angular-cli) version 21.1.1.

## Description

AcapAngularUi is a minimal Angular (standalone) frontend intended to run inside an ACAP application. It provides a small UI to read and update multicast settings through a backend API exposed by the camera/ACAP.

The app uses Angular standalone components (no `AppModule`) and is configured for static/subpath hosting via `baseHref: "./index.html"`.

## Functionality

- Loads current multicast settings from the backend (`GET /info` via `ApiService`).
- Lets the user edit:
  - `MulticastAddress`
  - `MulticastPort`
- Saves settings back to the backend (`POST/PUT /param` via `ApiService`).
- Shows the latest backend response as formatted JSON.
- Handles loading/saving states and displays readable error messages.

## Architecture

High-level flow:

1) Bootstrap
   - `src/main.ts` bootstraps the standalone root component.
2) Root component
   - `src/app/app.ts` renders the multicast settings feature.
3) Feature component
   - `src/app/multicast-settings/multicast-settings.ts` owns the page state and UI behavior.
4) API layer
   - `src/app/api.ts` encapsulates HTTP calls and response shapes.

Key files and folders:

- `src/app/app.ts` — standalone root component.
- `src/app/multicast-settings/` — multicast settings feature (template, styles, logic, spec).
- `src/app/api.ts` — HTTP API service and interfaces.
- `angular.json` — build/serve config (including `baseHref: "./"` for ACAP hosting).

## Development server

To start a local development server, run:

```bash
ng serve
```

Once the server is running, open your browser and navigate to `http://localhost:4200/`. The application will automatically reload whenever you modify any of the source files.

## Code scaffolding

Angular CLI includes powerful code scaffolding tools. To generate a new component, run:

```bash
ng generate component component-name
```

For a complete list of available schematics (such as `components`, `directives`, or `pipes`), run:

```bash
ng generate --help
```

### Note 

1. Add this into `angular.json`:

```json
"build": {
            ...
          "options": {
            "baseHref": "./index.html",
            "polyfills": ["zone.js"],
            ...
          }
}
```

Install previously zone.js to avoid errors on change detection not running after the async callback (zoneless setup with non-signal state), i.e. you update class fields, but Angular never re-renders those parts of the template.


```bash
npm install zone.js
```

Check `app.config.ts` file, it should contain `provideZoneChangeDetection`

2. Change in `index.html` 

```html
<base href="/"> 

```

to

```html
<base href="./index.html"> 

```


"./" makes the built URLs relative, which is usually the safest for ACAP/static hosting without server rewrites. Because you are serving the app from a subpath like `/local/<app>/...`

*** This will solve apache issue serving non index.html path ***





## Building

To build the project run:

```bash
ng build --configuration production
```

This will compile your project and store the build artifacts in the `dist/` directory. By default, the production build optimizes your application for performance and speed.

## Add content to acap

```bash
cp -r dist/acap-angular-ui/* my_acap_app/app/html/
```