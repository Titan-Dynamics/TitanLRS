import {html, LitElement} from 'lit'
import {customElement, query, state} from 'lit/decorators.js'
import '../components/filedrag.js'
import {cuteAlert, postWithFeedback} from "../utils/feedback.js"

@customElement('lr1121-updater')
export class LR1121Updater extends LitElement {
    @query('#radio2') accessor radio2

    @state() accessor data = undefined
    @state() accessor status = ''
    @state() accessor progress = 0
    @state() accessor manual = false

    createRenderRoot() {
        return this
    }

    render() {
        return html`
            <div class="td-h2" style="margin-bottom: var(--td-s-4);">LR1121 Firmware</div>

            ${this.manual ? html`
                <div class="td-card" style="margin-bottom: var(--td-s-4); background: var(--td-warn-soft); border-color: var(--td-warn);">
                    <div class="td-card-body td-small" style="color: var(--td-warn);">
                        LR1121 firmware has been manually flashed. Click below to revert to the TitanLRS provided version.
                        <div style="margin-top: var(--td-s-3);">
                            <button class="td-btn" @click=${this._reset}>Reset and reboot</button>
                        </div>
                    </div>
                </div>
            ` : ''}

            <div class="td-card" style="margin-bottom: var(--td-s-4);">
                <div class="td-card-header">
                    <span class="td-h4">Flash firmware</span>
                    ${this.status ? html`<span class="td-chip td-chip-info">${this.status}</span>` : ''}
                </div>
                <div class="td-card-body">
                    ${this._renderRadios()}
                    <p class="td-small td-mute" style="margin-bottom: var(--td-s-3);">Upload LR1121 firmware binary:</p>
                    <file-drop label="Upload and Flash" @file-drop=${this._fileSelected}>or drop firmware file here</file-drop>
                    ${this.status ? html`
                        <div class="td-progress-wrap">
                            <div class="td-progress-label">${this.status}</div>
                            <progress class="td-progress" .value="${this.progress}" max="100"></progress>
                        </div>
                    ` : ''}
                </div>
            </div>

            ${this.data ? html`
                <div class="td-card">
                    <div class="td-card-header">
                        <span class="td-h4">Radio information</span>
                    </div>
                    ${this._renderInfoTable()}
                </div>
            ` : ''}
        `
    }

    _renderRadios() {
        if (!this.data?.radio2) return html``
        return html`
            <div class="td-card-row" style="border-bottom: none; padding-bottom: 0; margin-bottom: var(--td-s-3);">
                <span class="td-label">Target radio</span>
                <div class="td-segment" style="width: fit-content;">
                    <button id="radio1" type="button" class="is-active">Radio 1</button>
                    <button id="radio2" type="button">Radio 2</button>
                </div>
            </div>
        `
    }

    _renderInfoTable() {
        if (!this.data) return html``
        const r1 = this.data.radio1
        const r2 = this.data.radio2
        return html`
            <table class="td-table">
                <thead>
                    <tr>
                        <th>Parameter</th>
                        <th>Radio 1</th>
                        ${r2 ? html`<th>Radio 2</th>` : ''}
                    </tr>
                </thead>
                <tbody>
                    <tr>
                        <td class="td-mute">Type</td>
                        <td class="td-mono">${this._dec2hex(r1?.type, 2)}</td>
                        ${r2 ? html`<td class="td-mono">${this._dec2hex(r2.type, 2)}</td>` : ''}
                    </tr>
                    <tr>
                        <td class="td-mute">Hardware</td>
                        <td class="td-mono">${this._dec2hex(r1?.hardware, 2)}</td>
                        ${r2 ? html`<td class="td-mono">${this._dec2hex(r2.hardware, 2)}</td>` : ''}
                    </tr>
                    <tr>
                        <td class="td-mute">Firmware</td>
                        <td class="td-mono">${this._dec2hex(r1?.firmware, 4)}</td>
                        ${r2 ? html`<td class="td-mono">${this._dec2hex(r2.firmware, 4)}</td>` : ''}
                    </tr>
                </tbody>
            </table>
        `
    }

    connectedCallback() {
        super.connectedCallback()
        this._loadData()
    }

    _dec2hex(i, len) {
        if (i === undefined || i === null) return ''
        return "0x" + (i + 0x10000).toString(16).substr(-len).toUpperCase()
    }

    _reset(e) {
        e.preventDefault()
        e.stopPropagation()
        return postWithFeedback('LR1121 Reset', 'Reset failed', '/reset?lr1121', null)(e)
    }

    _loadData() {
        const xmlhttp = new XMLHttpRequest()
        xmlhttp.onreadystatechange = () => {
            if (xmlhttp.readyState === 4 && xmlhttp.status === 200) {
                const data = JSON.parse(xmlhttp.responseText)
                this.data = data
                this.manual = !!data.manual
            }
        }
        xmlhttp.open('GET', '/lr1121.json', true)
        xmlhttp.setRequestHeader('Content-type', 'application/x-www-form-urlencoded')
        xmlhttp.send()
    }

    _fileSelected(e) {
        const files = e.detail.files
        if (files && files[0]) this._uploadFile(files[0])
    }

    _uploadFile(file) {
        const ajax = new XMLHttpRequest()
        ajax.upload.addEventListener('progress', (event) => this._progressHandler(event), false)
        ajax.addEventListener('load', (event) => this._completeHandler(event), false)
        ajax.addEventListener('error', (event) => this._errorHandler(event), false)
        ajax.addEventListener('abort', (event) => this._abortHandler(event), false)
        ajax.open('POST', '/lr1121')
        ajax.setRequestHeader('X-FileSize', file.size)
        const radio = document.querySelector('input[name=optionsRadio]:checked')?.value || '1'
        ajax.setRequestHeader('X-Radio', radio)
        const formdata = new FormData()
        formdata.append('upload', file, file.name)
        ajax.send(formdata)
    }

    _progressHandler(event) {
        const percent = Math.round((event.loaded / event.total) * 100)
        this.progress = percent
        this.status = percent + '% uploaded'
        this.requestUpdate()
    }

    async _completeHandler(event) {
        this.status = ''
        this.progress = 0
        const data = JSON.parse(event.target.responseText || '{}')
        if (data.status === 'ok') {
            let percent = 0
            const interval = setInterval(async () => {
                percent = percent + 2
                this.progress = percent
                this.status = percent + '% flashed'
                this.requestUpdate()
                if (percent >= 100) {
                    clearInterval(interval)
                    this._resetProgress()
                    await cuteAlert({type: 'success', title: 'Update Succeeded', message: data.msg})
                }
            }, 100)
        } else {
            await cuteAlert({type: 'error', title: 'Update Failed', message: data.msg || ''})
        }
    }

    _errorHandler(event) {
        this._resetProgress()
        return cuteAlert({type: 'error', title: 'Update Failed', message: event?.target?.responseText || ''})
    }

    _abortHandler(event) {
        this._resetProgress()
        return cuteAlert({type: 'info', title: 'Update Aborted', message: event?.target?.responseText || ''})
    }

    _resetProgress() {
        this.status = ''
        this.progress = 0
        this.requestUpdate()
    }
}
