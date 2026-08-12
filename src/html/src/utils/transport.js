/*
 * transport.js — the single seam between the panels and however the device is reached.
 *
 * The default implementation is the on-device one: plain HTTP against the firmware's own
 * web server, exactly as the panels used to do inline. The web flasher's Device Dashboard
 * installs a WebSerial implementation instead via setTransport(), which is why every panel
 * data access must go through here and no panel may call fetch()/XHR directly.
 *
 * Any replacement transport must implement this whole interface:
 *
 *   getConfig({export: bool})      -> Promise<object>   the /config document
 *   saveConfig(object)             -> Promise<string>   resolves with the device's status text
 *   saveOptions(object)            -> Promise<string>
 *   importConfig(jsonText)         -> Promise<string>   models.json import (TX)
 *   previewButtonColors(array)     -> Promise<void>     optional; cosmetic LED preview
 *   reboot()                       -> Promise<void>
 *   reset({config, options, hardware, lr1121}) -> Promise<void>
 *   exportConfig({export: bool})   -> Promise<string>   raw JSON text for download
 *
 * Errors reject with an Error whose .message is the device's response text where available.
 */

async function request(method, url, body, headers = {}) {
  const init = {method, headers: {...headers}}
  if (body !== undefined && body !== null) {
    if (!init.headers['Content-Type']) init.headers['Content-Type'] = 'application/json'
    init.body = typeof body === 'string' ? body : JSON.stringify(body)
  }
  const resp = await fetch(url, init)
  const text = await resp.text().catch(() => '')
  if (!resp.ok) throw new Error(text || `Request failed (${resp.status})`)
  return text
}

export const httpTransport = {
  name: 'http',

  async getConfig({export: exportMode = false} = {}) {
    const text = await request('GET', exportMode ? '/config?export' : '/config')
    return JSON.parse(text)
  },

  saveConfig(config) {
    return request('POST', '/config', config)
  },

  saveOptions(options) {
    return request('POST', '/options.json', options)
  },

  importConfig(jsonText) {
    return request('POST', '/import', jsonText)
  },

  // Live LED preview while dragging the button colour picker. Cosmetic only — a transport
  // that has no equivalent may make this a no-op.
  previewButtonColors(colors) {
    return request('POST', '/buttons', colors)
  },

  async reboot() {
    await request('POST', '/reboot')
  },

  async reset(flags = {config: true}) {
    const args = Object.entries(flags).filter(([, v]) => v).map(([k]) => k)
    await request('POST', '/reset' + (args.length ? '?' + args.join('&') : ''))
  },

  exportConfig({export: exportMode = true} = {}) {
    return request('GET', exportMode ? '/config?export' : '/config')
  },
}

let current = httpTransport

/** Install a different transport (the flasher dashboard does this at connect time). */
export function setTransport(impl) {
  current = impl
}

export function getTransport() {
  return current
}

/*
 * The panels import this object and call through it. It is a stable proxy so that swapping the
 * underlying implementation at runtime is invisible to already-imported modules.
 */
export const transport = {
  get name() { return current.name },
  getConfig: (...a) => current.getConfig(...a),
  saveConfig: (...a) => current.saveConfig(...a),
  saveOptions: (...a) => current.saveOptions(...a),
  importConfig: (...a) => current.importConfig(...a),
  previewButtonColors: (...a) => (current.previewButtonColors ? current.previewButtonColors(...a) : Promise.resolve()),
  reboot: (...a) => current.reboot(...a),
  reset: (...a) => current.reset(...a),
  exportConfig: (...a) => current.exportConfig(...a),
}

/**
 * Fetch an export document and hand it to the browser as a download. Works for both transports
 * because it builds the blob client-side rather than relying on a device URL.
 */
export async function downloadExport(filename, opts = {export: true}) {
  const text = await transport.exportConfig(opts)
  const url = URL.createObjectURL(new Blob([text], {type: 'application/json'}))
  const a = document.createElement('a')
  a.href = url
  a.download = filename
  document.body.appendChild(a)
  a.click()
  a.remove()
  setTimeout(() => URL.revokeObjectURL(url), 1000)
}
