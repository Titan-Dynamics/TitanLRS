import {html, LitElement, nothing} from 'lit'
import {customElement, state} from 'lit/decorators.js'
import {postWithFeedback, saveJSONWithReboot} from '../utils/feedback.js'
import '../components/filedrag.js'
import HARDWARE_SCHEMA from '../utils/hardware-schema.js'
import {_arrayInput, _intInput, _uintInput} from "../utils/libs.js";

@customElement('hardware-layout')
export class HardwareLayout extends LitElement {

    @state() accessor customised = false

    static SCHEMA = HARDWARE_SCHEMA

    createRenderRoot() {
        return this
    }

    render() {
        return html`
            <div class="td-h2" style="margin-bottom: var(--td-s-4);">Hardware Layout</div>

            <div class="td-card" style="margin-bottom: var(--td-s-4);">
                <div class="td-card-header">
                    <span class="td-h4">Upload configuration</span>
                </div>
                <div class="td-card-body">
                    <p class="td-small td-mute" style="margin-bottom: var(--td-s-3);">
                        Upload target configuration file, then press "Save" below.
                    </p>
                    <file-drop id="filedrag" label="Upload" @file-drop=${this._onFileDrop}>or drop files here</file-drop>
                </div>
            </div>

            <div class="td-card">
                <div class="td-card-header">
                    <span class="td-h4">Pin configuration</span>
                    ${this.customised ? html`<span class="td-chip td-chip-warn">Customised</span>` : ''}
                </div>

                ${this.customised ? html`
                    <div class="td-card-body td-warning-banner">
                        This hardware configuration has been customised. Safe to ignore for custom hardware builds.
                        <a download href="/hardware.json" style="color: var(--td-brand);">Download</a> or
                        <a href="/reset?hardware" style="color: var(--td-bad);"
                           @click="${postWithFeedback('Hardware Configuration Reset', 'Reset failed', '/reset?hardware')}">reset</a>
                        to defaults.
                    </div>
                ` : ''}

                <form id="upload_hardware">
                    ${this._renderTable()}
                    <div style="padding: var(--td-s-3) var(--td-s-4); border-top: 1px solid var(--td-line);">
                        <button type="button" class="td-btn td-btn-primary" @click=${this._submitConfig}>
                            Save Target Configuration
                        </button>
                    </div>
                </form>
            </div>
        `
    }

    _renderTable() {
        return html`
            <table class="td-table">
                <tbody>
                ${this.constructor.SCHEMA.map(section => html`
                    <tr class="td-table-section-header">
                        <td colspan="3">
                            <span class="td-xs">${section.title}</span>
                        </td>
                    </tr>
                    ${section.rows.map(row => html`
                        <tr>
                            <td style="color: var(--td-fg-mute);">${row.label}${this._renderIcon(row.icon)}</td>
                            <td>${this._renderField(row)}</td>
                            <td class="td-small td-mute">${row.desc || ''}</td>
                        </tr>
                    `)}
                `)}
                </tbody>
            </table>
        `
    }

    _renderIcon(icon) {
        if (!icon) return html``
        if (icon === 'input-output') {
            return html`<img class="icon-input"/><img class="icon-output"/>`
        }
        return html`<img class="icon-${icon}"/>`
    }

    _renderField(row) {
        switch (row.type) {
            case 'checkbox':
                return html`<input id="${row.id}" name="${row.id}" type="checkbox" class="td-check"/>`
            case 'select':
                return html`<select id="${row.id}" name="${row.id}" class="td-select" style="width: auto;">
                    ${row.options?.map(opt => html`
                        <option value="${opt.value}">${opt.label}</option>`)}
                </select>`
            case 'int':
                return html`<input id="${row.id}" name="${row.id}" size=${row.size ?? 3} maxlength=${row.size ?? 3}
                                   type="text" class="td-input td-input-mono" style="width: 60px;"
                                   @keypress="${_intInput}"/>`
            case 'uint':
                return html`<input id="${row.id}" name="${row.id}" size=${row.size ?? 3} maxlength=${row.size ?? 3}
                                   type="text" class="td-input td-input-mono" style="width: 60px;"
                                   @keypress="${_uintInput}"/>`
            case 'array':
                return html`<input id="${row.id}" name="${row.id}" size=${row.size ?? nothing}
                                   maxlength=${row.size ?? nothing} type="text" class="td-input td-input-mono array"
                                   @keypress="${_arrayInput}"/>`
        }
    }

    connectedCallback() {
        super.connectedCallback()
        setTimeout(() => this._initTooltips(), 0)
        this._loadData()
    }

    _initTooltips() {
        const add = (cls, label) => {
            document.querySelectorAll('.' + cls).forEach(i => i.setAttribute('title', label))
        }
        add('icon-input', 'Digital Input')
        add('icon-output', 'Digital Output')
        add('icon-analog', 'Analog Input')
        add('icon-pwm', 'PWM Output')
    }

    _loadData() {
        const xmlhttp = new XMLHttpRequest()
        xmlhttp.onreadystatechange = () => {
            if (xmlhttp.readyState === 4 && xmlhttp.status === 200) {
                const data = JSON.parse(xmlhttp.responseText)
                this.customised = !!data.customised
                this._updateHardwareSettings(data)
            }
        }
        xmlhttp.open('GET', '/hardware.json', true)
        xmlhttp.setRequestHeader('Content-type', 'application/x-www-form-urlencoded')
        xmlhttp.send()
    }

    _onFileDrop(e) {
        const files = e.detail.files
        const form = document.getElementById('upload_hardware')
        if (form) form.reset()
        for (const file of files) {
            const reader = new FileReader()
            reader.onload = (ev) => {
                const data = JSON.parse(ev.target.result)
                this._updateHardwareSettings(data)
            }
            reader.readAsText(file)
        }
    }

    _updateHardwareSettings(data) {
        for (const [key, value] of Object.entries(data)) {
            const el = document.getElementById(key)
            if (el) {
                if (el.type === 'checkbox') {
                    el.checked = !!value
                } else {
                    el.value = Array.isArray(value) ? value.toString() : value
                }
            }
        }
    }

    _submitConfig() {
        const form = document.getElementById('upload_hardware')
        const formData = new FormData(form)
        const body = JSON.stringify(Object.fromEntries(formData), (k, v) => {
            if (v === '') return undefined
            const el = document.getElementById(k)
            if (el && el.type === 'checkbox') return v === 'on'
            if (el && el.classList.contains('array')) {
                const arr = v.split(',').map((element) => Number(element))
                return arr.length === 0 ? undefined : arr
            }
            return isNaN(v) ? v : +v
        })
        saveJSONWithReboot('Upload Succeeded', 'Upload Failed', '/hardware.json', {...JSON.parse(body), "customised": true})
        return false
    }
}
