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
* **Queue sizes** — Depth for `updater` and `worker` components
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
`updater.UPDATE_IMAGE_FROM(new_file, new_crc32)` to install the reconstructed image. A deployment may instead wire
`deltaPatcher.patchComplete -> worker.updateImage` to install automatically. See
`FprimeExtras/Update/DeltaPatcher/docs/sdd.md`.
