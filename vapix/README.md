## VAPIX API Samples

This folder contains two ACAP-native examples demonstrating how to interact with Axis cameras using VAPIX and ONVIF APIs from within an app.
Both samples highlight different ways to control video streams and overlays through HTTP/JSON or SOAP requests.

## Samples Overview

1. dynamic-overlay-vapix

    - Purpose: Add and manage text overlays on a video stream using the Dynamic Overlay VAPIX API.
    - Key features:

        - Retrieves VAPIX service account credentials over D-Bus (no hard-coded secrets).
        - Builds a JSON request to dynamicoverlay.cgi (addText method).
        - Posts the JSON payload with libcurl and parses the response with jansson.

    - What you’ll learn:

        - Securely accessing VAPIX credentials.
        - Constructing JSON requests.
        - Modifying overlay properties (text, position, font, colors).



2. onvif-request

- Purpose: Configure video encoder properties using the ONVIF Media2 API with SOAP.
- Key features:

    - Retrieves ONVIF credentials via the same D-Bus VAPIX account interface.
    - Builds and posts a SetVideoEncoderConfiguration SOAP request to /vapix/services.
    - Configures properties like:

        - Resolution
        - Frame rate 
        - Bitrate limit
        - Multicast address and port
        - Encoding profile (H.264)

- What you’ll learn:

- Building SOAP XML envelopes in C.
- Sending authenticated ONVIF requests with libcurl.
- Handling HTTP status codes and SOAP responses.

Use case example: Dynamically reconfigure a camera’s encoding settings (e.g., switch to multicast 4K@25fps).

## Common Concepts

Both samples demonstrate:

- Using D-Bus to fetch VAPIX credentials → avoids embedding usernames/passwords in code.
- Making HTTP(S) requests with libcurl → JSON-RPC for VAPIX, SOAP/XML for ONVIF.
- System logging with syslog → consistent runtime feedback.
- Error handling with panic() → ensures invalid states exit clearly.


These samples provide a foundation for apps that both display information on a video stream and reconfigure video encoder settings dynamically using Axis camera APIs.