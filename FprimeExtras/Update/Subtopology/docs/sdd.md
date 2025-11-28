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