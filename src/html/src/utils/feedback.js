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

// Helper to post JSON and then show reboot prompt on success
export function saveJSONWithReboot(title, errorTitle, url, changes, successCB) {
  postJSON(url, changes, {
    onload: async () => {
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
        const r = new XMLHttpRequest()
        r.open('POST', '/reboot')
        r.setRequestHeader('Content-Type', 'application/json')
        r.send()
      }
    },
    onerror: async (xhr) => {
      await errorAlert(errorTitle, xhr.responseText || 'Request failed')
    }
  })
}

export function postWithFeedback(title, errorMsg, url, getdata, success) {
  return function (e) {
    e.stopPropagation()
    e.preventDefault()
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
    let data
    if (getdata) data = getdata(xmlhttp)
    else data = null
    xmlhttp.send(data)
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
