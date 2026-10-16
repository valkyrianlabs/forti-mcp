---
title: Several FortiGates
navTitle: Several FortiGates
description: Manage several FortiGates from one forti-mcp with a profile per device, --profile and the per-request profile field.
order: 60
status: published
tags:
  - guides
---

# Several FortiGates

One forti-mcp serves every FortiGate in the forti home. Each device is a **profile** in
`~/.forti/config.json`, with its own host, credentials, certificates and mode. The model picks a
device per request with the `profile` field; `--profile` sets the device used when a request names
none.

:::toc[On this page]{depth="2" theme="compact"}
:::

## A profile per device

Create a profile for each FortiGate with the `forti` CLI. Profile names are 1 to 64 letters, digits,
`-` and `_`.

```bash
forti configure --profile fw01 --host fw01.example.net --api-key-stdin --default < fw01.key
forti profile copy fw01 fw02 && forti profile set fw02 host=fw02.example.net
forti configure --profile fw02 --api-key-stdin < fw02.key
forti profile list
```

`forti profile copy` copies the settings but no secrets, so the copy gets its own API key from
`forti configure`. Other profile commands:

| Command | What it does |
|---|---|
| `forti profile default NAME` | Makes `NAME` the default profile |
| `forti profile mode [NAME] MODE` | Sets a profile's mode: `read-only`, `guarded` or `autonomous` |
| `forti profile set NAME key=value...` | Changes settings such as `port`, `vdom` or `ca_bundle` without contacting the device |
| `forti profile rename OLD NEW` | Renames a profile with its stored secrets and certificates |
| `forti cert use CERT --profile NAME` | Gives a profile a client certificate; one certificate can serve several FortiGates (`forti cert list`) |

forti-mcp reads `config.json` once, at the first request that needs it, and keeps it for the life
of the process. After adding or changing a profile, start a new agent session so the client starts
forti-mcp again.

## How a request picks its device

`forti_discover`, `forti_read` and `forti_plan` accept a `profile` field. A request uses:

1. the `profile` it names;
2. else the registration's `--profile`;
3. else the default profile of the forti home (`forti profile default`);
4. else the only profile, when there is exactly one.

With several profiles and no default, a request that names none fails with a `config` error that
says so. `forti_apply` takes only a `plan_id`: a stored plan already names its profile.

The model learns the devices from `forti_discover`:

```json
{"what": "devices"}
{"what": "capabilities", "profile": "fw02"}
```

`devices` lists every profile with host, port and mode (never a secret) and reports the default in
`meta.default_profile`. After that, the model names the device in each request:

```json
{"resource": "firewall/address", "profile": "fw02", "fields": ["name", "subnet"], "count": 50}
{"profile": "fw02", "operations": [{"op": "create", "resource": "firewall/address",
  "object": {"name": "web", "subnet": "192.0.2.10 255.255.255.255"}}]}
```

Read and plan results name the profile they used in `meta.profile`.

## A default device for a registration

`--profile` in the client entry sets step 2 above. `forti install mcp` writes it for you:

```bash
forti install mcp --client all --scope workspace --profile fw01
```

The tools' schemas then name that profile as the default, so the model sees which device it gets
when it names none.

:::callout[--profile is a default, not a restriction]{variant="warning"}
A registration with `--profile fw01` can still reach every other profile in the forti home by
naming it. What limits each device is its profile's mode and the FortiOS account's privileges
([Safety and modes](/guides/safety-and-modes)).
:::

## A mode per device

Each profile has its own mode, so one workspace can hold a production FortiGate in `read-only` or
`guarded` mode next to a lab device in `autonomous` mode:

```bash
forti profile mode fw01 guarded
forti profile mode lab autonomous
```

`forti_discover` reports each profile's mode, and `forti_plan` says what apply will do in it. A
guarded plan on one device is approved with `forti plan approve <id>` like any other; the plan id
already identifies the device.
