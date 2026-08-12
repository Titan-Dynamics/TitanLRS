import {transport} from './transport.js'

export function infoAlert(title, message) {
  return cuteAlert({ type: 'info', title, message })
}
export function errorAlert(title, message) {
  return cuteAlert({ type: 'error', title, message })
}

// Generic POST helper that can send JSON or raw bodies (no UI side-effects)
export function post(url, data, { headers = {}, onload, onerror } = {}) {
  const xhr = new XMLHttpRequest()
  xhr.onreadystatechange = function () {
    if (xhr.readyState === 4) {
      if (xhr.status === 200) {
        if (onload) onload(xhr)
      } else {
        if (onerror) onerror(xhr)
      }
    }
  }
  xhr.open('POST', url, true)
  const isFormData = (typeof FormData !== 'undefined') && (data instanceof FormData)
  if (!isFormData) {
    // default to JSON unless caller supplied explicit Content-Type
    xhr.setRequestHeader('Content-Type', headers['Content-Type'] || 'application/json')
  }
  for (const [k, v] of Object.entries(headers)) {
    if (k.toLowerCase() !== 'content-type') xhr.setRequestHeader(k, v)
  }
  const payload = isFormData ? data : (typeof data === 'string' ? data : JSON.stringify(data))
  xhr.send(payload)
  return xhr
}

// Convenience wrapper for JSON bodies
export function postJSON(url, data, opts = {}) {
  return post(url, data, opts)
}

// Save settings that only take effect at boot: confirm up front, then save and reboot without a
// second prompt. Cancelling leaves the device untouched — nothing is written. The button that
// triggers this reads "Save & Reboot", so the confirmation is never a surprise.
export function saveAndReboot(title, errorTitle, saveFn, changes, successCB,
                              message = 'These settings are applied when the device boots. Save them and reboot now?') {
  return cuteAlert({
    type: 'question',
    title,
    message,
    confirmText: 'Save & Reboot',
    cancelText: 'Cancel',
  }).then((res) => {
    if (res !== 'confirm') return
    return Promise.resolve()
      .then(() => saveFn(changes))
      .then(() => {
        if (successCB) successCB()
        // fire-and-forget reboot
        Promise.resolve(transport.reboot()).catch(() => {})
      })
      .catch(async (err) => {
        await errorAlert(errorTitle, (err && err.message) || 'Request failed')
      })
  })
}

// Run a save action (a function returning a Promise, normally a transport method) and then
// show the reboot prompt on success. `saveFn` replaces what used to be a hard-coded URL, which
// is what lets the same panels run over HTTP on-device and over WebSerial in the web flasher.
export function saveWithReboot(title, errorTitle, saveFn, changes, successCB) {
  return Promise.resolve()
    .then(() => saveFn(changes))
    .then(async () => {
      let message
      if (successCB) message = successCB()
      const res = await cuteAlert({
        type: 'question',
        title,
        message: message || 'Reboot to take effect',
        confirmText: 'Reboot',
        cancelText: 'Close',
      })
      if (res === 'confirm') {
        // fire-and-forget reboot
        Promise.resolve(transport.reboot()).catch(() => {})
      }
    })
    .catch(async (err) => {
      await errorAlert(errorTitle, (err && err.message) || 'Request failed')
    })
}

// URL-based variant, kept for the ESP-only panels that are NOT part of the transport
// abstraction (wifi, update, lr1121-updater, hardware-layout — see transport.js).
export function saveJSONWithReboot(title, errorTitle, url, changes, successCB) {
  return saveWithReboot(title, errorTitle, (data) => postJSONAsync(url, data), changes, successCB)
}

function postJSONAsync(url, data) {
  return new Promise((resolve, reject) => {
    postJSON(url, data, {
      onload: (xhr) => resolve(xhr.responseText),
      onerror: (xhr) => reject(new Error(xhr.responseText || 'Request failed')),
    })
  })
}

// URL-based click handler, kept for the same ESP-only panels as above.
export function postWithFeedback(title, errorMsg, url, getdata, success) {
  return function (e) {
    if (e) {
      e.stopPropagation()
      e.preventDefault()
    }
    const xmlhttp = new XMLHttpRequest()
    xmlhttp.onreadystatechange = async function () {
      if (this.readyState === 4) {
        if (this.status === 200) {
          if (success) success()
          await infoAlert(title, this.responseText)
        } else {
          await errorAlert(title, errorMsg)
        }
      }
    }
    xmlhttp.open('POST', url, true)
    xmlhttp.send(getdata ? getdata(xmlhttp) : null)
  }
}

// Confirm a destructive action first, then run it with no success popup — the visible outcome (a
// reboot, a re-render) is the feedback. Failures still surface. Cancelling does nothing at all.
export function actionWithConfirm(title, message, confirmText, errorTitle, actionFn) {
  return function (e) {
    if (e) {
      e.stopPropagation()
      e.preventDefault()
    }
    return cuteAlert({type: 'question', title, message, confirmText, cancelText: 'Cancel'})
      .then((res) => {
        if (res !== 'confirm') return
        return Promise.resolve()
          .then(() => actionFn())
          .catch(async (err) => {
            await errorAlert(errorTitle, (err && err.message) || 'Request failed')
          })
      })
  }
}

// Click handler that runs a transport action and reports the outcome.
export function actionWithFeedback(title, errorMsg, actionFn, success) {
  return function (e) {
    if (e) {
      e.stopPropagation()
      e.preventDefault()
    }
    return Promise.resolve()
      .then(() => actionFn())
      .then(async (responseText) => {
        if (success) success()
        await infoAlert(title, responseText || 'Done')
      })
      .catch(async () => {
        await errorAlert(title, errorMsg)
      })
  }
}

export function cuteAlert({
  type,
  title,
  message,
  buttonText = 'OK',
  confirmText = 'OK',
  cancelText = 'Cancel',
}) {
  return new Promise((resolve) => {
    const headerClass = {
      error: 'is-error', success: 'is-success', info: 'is-info',
      question: 'is-question', warn: 'is-warn'
    }[type] || 'is-info'

    const iconName = {
      error: 'activity', success: 'activity', info: 'info',
      question: 'bell', warn: 'bell'
    }[type] || 'info'

    const actions = type === 'question'
      ? `<button class="td-btn td-btn-danger td-alert-confirm">${confirmText}</button>
         <button class="td-btn td-alert-cancel">${cancelText}</button>`
      : `<button class="td-btn td-btn-primary td-alert-ok">${buttonText}</button>`

    const wrapper = document.createElement('div')
    wrapper.className = 'td-alert-backdrop'
    wrapper.innerHTML = `
<div class="td-alert-card">
  <div class="td-alert-header ${headerClass}">
    <span class="td-h4">${title}</span>
    <button class="td-alert-close" aria-label="Close">&times;</button>
  </div>
  <div class="td-alert-body">
    <span class="td-alert-message">${message}</span>
    <div class="td-alert-actions">${actions}</div>
  </div>
</div>`

    document.body.appendChild(wrapper)

    const card = wrapper.querySelector('.td-alert-card')

    function resolveIt() { wrapper.remove(); resolve() }
    function confirmIt() { wrapper.remove(); resolve('confirm') }

    wrapper.querySelector('.td-alert-close').addEventListener('click', resolveIt)
    wrapper.addEventListener('click', resolveIt)
    card.addEventListener('click', e => e.stopPropagation())

    if (type === 'question') {
      wrapper.querySelector('.td-alert-confirm').addEventListener('click', confirmIt)
      wrapper.querySelector('.td-alert-cancel').addEventListener('click', resolveIt)
    } else {
      wrapper.querySelector('.td-alert-ok').addEventListener('click', resolveIt)
    }
  })
}
