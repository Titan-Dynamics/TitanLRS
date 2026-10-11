import {html, LitElement, nothing} from 'lit'
import {customElement, state} from 'lit/decorators.js'
import {loadJSON, postWithFeedback, saveJSONWithReboot} from '../utils/feedback.js'
import '../components/filedrag.js'
import HARDWARE_SCHEMA from '../utils/hardware-schema.js'
import {_arrayInput, _floatInput, _intInput, _uintInput} from "../utils/libs.js";

@customElement('hardware-layout')
export class HardwareLayout extends LitElement {

    @state() accessor customised = false
    @state() accessor loadedHardwareJson = null
    @state() accessor currentHardwareJson = '{}'
    loadPromise = null

    static SCHEMA = HARDWARE_SCHEMA

    // Rows set when the device is flashed; shown, never edited or saved.
    static READONLY = new Set(HARDWARE_SCHEMA.flatMap(section => section.rows).filter(row => row.readonly).map(row => row.id))

    createRenderRoot() {
        this._onFormEdited = this._onFormEdited.bind(this)
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

                <form id="upload_hardware" @input=${this._onFormEdited} @change=${this._onFormEdited}>
                    ${this._renderTable()}
                    <div style="padding: var(--td-s-3) var(--td-s-4); border-top: 1px solid var(--td-line);">
                        <button type="button" class="td-btn td-btn-primary" @click=${this._submitConfig}
                                ?disabled=${this._isSaveDisabled()}>
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
            case 'float':
                return html`<input id="${row.id}" name="${row.id}" size=${row.size ?? 10} maxlength=${row.size ?? 10}
                                   type="text" class="td-input td-input-mono" style="width: 110px;"
                                   @keypress="${_floatInput}"/>`
            case 'int':
                return html`<input id="${row.id}" name="${row.id}" size=${row.size ?? 3} maxlength=${row.size ?? 3}
                                   type="text" class="td-input td-input-mono" style="width: 60px;"
                                   @keypress="${_intInput}"/>`
            case 'uint':
                if (row.readonly) {
                    return html`<input id="${row.id}" name="${row.id}" size=${row.size ?? 3} maxlength=${row.size ?? 3}
                                       type="text" class="td-input td-input-mono" readonly
                                       style="width: 60px; color: var(--td-fg-mute);"/>`
                }
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
        this.pageReady().catch(() => {})
    }

    // Called by the app after the page is shown, and from connectedCallback; loads the layout once.
    pageReady() {
        if (!this.loadPromise) {
            this.loadPromise = this.updateComplete
                .then(() => loadJSON('/hardware.json', 'Failed to load hardware configuration.'))
                .then((data) => {
                    this.customised = !!data.customised
                    this._updateHardwareSettings(data)
                    this.loadedHardwareJson = this.currentHardwareJson
                }, (error) => {
                    this.loadPromise = null
                    throw error
                })
        }
        return this.loadPromise
    }

    _field(id) {
        return document.getElementById(id)
    }

    _initTooltips() {
        const add = (cls, label) => {
            this.querySelectorAll('.' + cls).forEach(i => i.setAttribute('title', label))
        }
        add('icon-input', 'Digital Input')
        add('icon-output', 'Digital Output')
        add('icon-analog', 'Analog Input')
        add('icon-pwm', 'PWM Output')
    }

    _onFileDrop(e) {
        const files = e.detail.files
        const form = this._field('upload_hardware')
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
            const el = this._field(key)
            if (el) {
                if (el.type === 'checkbox') {
                    el.checked = !!value
                } else {
                    el.value = Array.isArray(value) ? value.toString() : value
                }
            }
        }
        this.currentHardwareJson = this._serializeCurrentConfig()
    }

    _submitConfig() {
        const body = this._serializeCurrentConfig()
        saveJSONWithReboot('Upload Succeeded', 'Upload Failed', '/hardware.json', {...JSON.parse(body), "customised": true}, () => {
            this.loadedHardwareJson = body
        })
        return false
    }

    _onFormEdited() {
        this.currentHardwareJson = this._serializeCurrentConfig()
    }

    // The form as the JSON that would be saved; also the dirty-tracking key. Read-only rows
    // (set when the device is flashed) are never saved.
    _serializeCurrentConfig() {
        const form = this._field('upload_hardware')
        if (!form) return '{}'
        const formData = new FormData(form)
        return JSON.stringify(Object.fromEntries(formData), (k, v) => {
            if (v === '' || this.constructor.READONLY.has(k)) return undefined
            const el = this._field(k)
            if (el && el.type === 'checkbox') return v === 'on'
            if (el && el.classList.contains('array')) {
                const arr = v.split(',').map((element) => Number(element))
                return arr.length === 0 ? undefined : arr
            }
            return isNaN(v) ? v : +v
        })
    }

    _isSaveDisabled() {
        if (this.loadedHardwareJson === null) return true
        return this.currentHardwareJson === this.loadedHardwareJson
    }

    checkChanged() {
        return !this._isSaveDisabled()
    }
}
