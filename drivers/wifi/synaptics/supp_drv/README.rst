Synaptics Wi-Fi Design Note
============================

Purpose
-------

This note explains the current Synaptics Wi-Fi integration in this workspace.
Synaptics Wi-Fi now supports building Zephyr's upstream Wi-Fi shell application
for SRW1500, while older chips continue to use the native API backend. The goal
is to let both generations coexist under one Zephyr-facing model and keep the
sample story simple and upstream-friendly.

Design Summary
--------------

The current design keeps one public Synaptics Wi-Fi feature switch,
``CONFIG_WIFI_SYNAPTICS``, and selects the implementation path from the chip
choice in Kconfig.

* Older chips such as 4381, 43711, and 4612 continue to use the native API
  backend.
* SRW1500 uses the supplicant-based backend.
* Backend selection is automatic from Kconfig, so application code does not
  need backend-specific logic.

This lets multiple chip generations live under one driver family instead of
creating separate product-specific integrations at the Zephyr API boundary.

Architecture Diagram
--------------------

.. code-block:: text

   flowchart TD
     A[Application or Sample] --> B[Zephyr Wi-Fi API and net_mgmt]
     B --> C[CONFIG_WIFI_SYNAPTICS]
     C --> D{Selected chip}

     D --> E[4381 / 43711 / 4612]
     D --> F[SRW1500]

     E --> G[SYNA_WIFI_BACKEND_NATIVE_API]
     F --> H[SYNA_WIFI_BACKEND_SUPPLICANT]

     G --> I[Synaptics native_api backend]
     H --> J[Synaptics supplicant backend]

     I --> K[MHD and chip-specific firmware assets]
     J --> K

     E --> L[Legacy validation sample: zephyr_srsdk/samples/wifi/synaptics]
     F --> M[Primary validation sample: zephyr/samples/net/wifi/shell + Synaptics overlays]

SRW1500 Runtime Path
--------------------

.. code-block:: text

   flowchart TD
     A[User runs Wi-Fi shell command] --> B[Zephyr Wi-Fi shell sample]
     B --> C[net_mgmt and Zephyr Wi-Fi APIs]
     C --> D[CONFIG_WIFI_SYNAPTICS]
     D --> E[CONFIG_WIFI_SRW1500]
     E --> F[CONFIG_SYNA_WIFI_BACKEND_SUPPLICANT]
     F --> G[Synaptics supplicant backend]
     G --> H[wpa_supplicant integration]
     H --> I[MHD adaptor and event bridge]
     I --> J[SRW1500 Wi-Fi firmware and transport]
     J --> K[Scan, connect, disconnect, status]

How Old Chips And New Chips Come Together
------------------------------------------

The merge point is the Synaptics driver family under the Wi-Fi driver tree.

* ``zephyr_srsdk/drivers/wifi/CMakeLists.txt`` exposes one Synaptics subtree.
* ``zephyr_srsdk/drivers/wifi/synaptics/Kconfig.synaptics`` provides one
  public enable switch and one chip-selection choice.
* ``zephyr_srsdk/drivers/wifi/synaptics/CMakeLists.txt`` routes the build to
  the appropriate backend.

The practical split is:

* Legacy SDIO-oriented chips keep the existing native API path.
* SRW1500 takes the supplicant path because that aligns better with the Zephyr
  Wi-Fi management flow used by the upstream shell sample.

The split happens below the Zephyr Wi-Fi API, not at the product-facing sample
layer:

* Both old and new chips enter through the same Zephyr Wi-Fi subsystem model.
* Chip choice decides the backend.
* The backend decides how it talks to MHD and chip-specific firmware assets.
* Only the validation sample strategy differs today: older chips still have a
  legacy Synaptics sample, while SRW1500 uses the upstream Wi-Fi shell sample
  as the main flow.

The shared flow is::

   Application -> Zephyr Wi-Fi APIs -> Synaptics driver family -> selected backend -> chip support package

The differing part is::

   Older chips: existing native_api backend + legacy Synaptics validation sample
   SRW1500: supplicant backend + upstream Zephyr wifi shell sample

Why SRW1500 Uses Wi-Fi Shell
----------------------------

For SRW1500, the primary flow is the upstream Zephyr Wi-Fi shell sample with a
Synaptics overlay and Synaptics driver enablement.

The main integration points are:

* ``zephyr/samples/net/wifi/shell/prj.conf`` for the upstream sample defaults.
* ``zephyr_srsdk/boards/syna/astra_sr/srw1500/srw1500_rdk_xip.conf`` for the
  SRW1500 RDK XIP configuration.
* ``zephyr_srsdk/samples/net/wifi/shell/synaptics/overlay_srw1500.conf`` for
  Synaptics SRW1500 enablement.
* ``zephyr_srsdk/boards/syna/astra_sr/srw1500/srw1500_rdk_xip.overlay`` for
  the board device-tree overlay.
* ``zephyr_srsdk/drivers/wifi/synaptics/Kconfig.synaptics`` for chip choice
  and backend selection.
* ``zephyr_srsdk/drivers/wifi/synaptics/CMakeLists.txt`` for backend build
  routing.

Build
-----

From the workspace root, build the upstream Zephyr Wi-Fi shell sample for the
SRW1500 RDK with a pristine build directory::

   west build --pristine -d build-wifi-shell-srw1500 -b srw1500_rdk zephyr/samples/net/wifi/shell -- \
     -DEXTRA_CONF_FILE="$PWD/zephyr_srsdk/boards/syna/astra_sr/srw1500/srw1500_rdk_xip.conf;$PWD/zephyr_srsdk/samples/net/wifi/shell/synaptics/overlay_srw1500.conf" \
     -DEXTRA_DTC_OVERLAY_FILE=$PWD/zephyr_srsdk/boards/syna/astra_sr/srw1500/srw1500_rdk_xip.overlay

Test
----

Flash the resulting image to the SRW1500 RDK and boot the system. The Zephyr
shell will be available after startup. Use the standard Zephyr Wi-Fi shell
commands to scan for networks and connect::

   wifi scan
   wifi connect -s myap -p mypassword -k 1

Replace ``myap`` and ``mypassword`` with the access point SSID and password.
The Wi-Fi shell also provides commands for disconnecting and checking
connection status. This exercises the standard Zephyr Wi-Fi shell workflow
through the Synaptics SRW1500 supplicant backend.

Why This Is Easier To Get Accepted Upstream
-------------------------------------------

This approach keeps the Zephyr-facing surface small.

* The sample entry point is the existing upstream sample,
  ``zephyr/samples/net/wifi/shell``.
* The Synaptics-specific sample delta is limited to board configuration and a
  Synaptics overlay.
* Driver-specific changes stay under the Synaptics Wi-Fi driver folder instead
  of creating a parallel sample framework.

This avoids introducing a custom application flow when the existing Zephyr
Wi-Fi shell already covers the common test and validation use cases.

In practice, this reduces review friction in two places:

* **Sample review:** reviewers see incremental changes around the existing
  Wi-Fi shell sample instead of a new vendor-owned sample that duplicates shell
  functionality.
* **Driver review:** reviewers can focus on the Synaptics driver implementation
  in the driver folder without justifying a second application model.

Current Scope And Non-Goals
---------------------------

Current scope:

* Keep a unified Synaptics Wi-Fi driver family.
* Reuse the upstream Zephyr Wi-Fi shell flow for SRW1500.
* Preserve support for older chips that still depend on the native API path.

Non-goals for now:

* No separate SRW1500 Wi-Fi sample owned by this team.
* No forced migration of all older-chip validation flows in one step.
* No backend-specific behavior exposed to applications unless a real product
  requirement makes that unavoidable.

Additional Design Ideas Discussed So Far
----------------------------------------

* Keep ``CONFIG_WIFI_SYNA`` only as a compatibility alias for existing
  Synaptics applications, while new integrations enable
  ``CONFIG_WIFI_SYNAPTICS`` directly.
* Keep chip-specific firmware, NVRAM, and CLM assets under the Synaptics HAL
  module so the application layer does not need per-chip file handling.
* Keep backend selection in Kconfig and build routing in CMake so the app layer
  stays simple.
* Limit Synaptics-specific sample additions to overlays and board configuration
  files where possible.
* Preserve the older ``zephyr_srsdk/samples/wifi/synaptics`` sample as a legacy
  validation path for native API chips until there is a clear reason to
  converge further.

Expected Evolution
------------------

The near-term architecture is a single Synaptics Wi-Fi family with two backend
implementations behind it:

* Native API backend for older chips.
* Supplicant backend for SRW1500-class devices.

If the long-term Zephyr flow for Synaptics converges further, the preferred
direction is to converge on common Zephyr networking workflows first and only
then reduce duplicate sample coverage. Until then, compatibility and clean
upstream layering are more important than forcing one internal implementation.
