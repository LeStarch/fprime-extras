# Update Subtopology — Software Design Document (SDD)

The Update Subtopology is used to provide a configurable setup for the Update configuration.

## Usage

To use this subtopology, import it.

```fpp
topology Flight {
  import Update.Subtopology
}
```

## Configuration

Configure component properties and worker component:
`FprimeExtras/Update/Subtopology/UpdateConfig/UpdateConfig.fpp`

### Component properties

* **Base ID** — Base identifier for the subtopologies; instance IDs are offset from this base.
* **Queue sizes** — Depth for `updater`, `worker` and `deltaPatcher` components (`deltaPatcher` must be ≥
  `DeltaPatcher::MAX_DISPATCH_PER_TICK`; excess commands are dropped)
* **Stack sizes** — Task stack allocation for `updater` and `worker` components
* **Priorities** — RTOS priorities for `updater` and `worker` components

### Configurable Components

`worker`
## Delta patching

The subtopology instantiates `deltaPatcher: Update.DeltaPatcher` (base id `BASE_ID + 0x2000`, queue size
`QueueSizes.deltaPatcher`). It is a queued component with no thread of its own: the deployment **must** connect a
rate group to `deltaPatcher.run`, e.g.

```fpp
rateGroup3.RateGroupMemberOut[N] -> Update.deltaPatcher.run
```

`deltaPatcher.patchComplete` is left unconnected. After `PatchComplete(new_file, new_crc32)` the operator issues
`updater.UPDATE_IMAGE_FROM(new_file, new_crc32)` to install the reconstructed image. `patchComplete` must **not** be
wired directly to `worker.updateImage`: the worker has a single client (`updater`), which owns the busy flag and the
command response delivered through `updateImageDone`. Automatic installation would require a new async input on
`Updater` that shares the `UPDATE_IMAGE_FROM` busy gate. Connecting `run` is mandatory: without a rate group the
component accepts commands into its queue but never dispatches them. Each `run` performs blocking `Os::File` I/O
(up to `DELTA_MAX_CHUNK_BYTES` bytes of output plus the corresponding old-image reads, or
`DELTA_VERIFY_BYTES_PER_STEP` bytes of CRC), so use a slow, non-critical rate group. See
`FprimeExtras/Update/DeltaPatcher/docs/sdd.md`.
