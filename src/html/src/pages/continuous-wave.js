import {html, LitElement} from 'lit';
import {customElement, query, state} from 'lit/decorators.js';
import FEATURES from "../features.js";
import {post} from "../utils/feedback.js";
import {elrsState} from "../utils/state.js";

@customElement('continuous-wave')
export class ContinuousWave extends LitElement {
    @query('#measured') accessor measured

    @state() accessor data = undefined
    @state() accessor started = false
    @state() accessor result = {}
    @state() accessor cwFreq;
    @state() accessor setSubGHz = false
    @state() accessor radio = 1

    _text = "Loading..."

    createRenderRoot() {
        return this
    }

    render() {
        if (this.data)
            return html`
                <div class="td-h2" style="margin-bottom: var(--td-s-4);">Continuous Wave</div>
                <div class="td-card">
                    <div class="td-card-header">
                        <span class="td-h4">RF transmission test</span>
                    </div>
                    <div class="td-card-body">
                        <p class="td-small td-mute" style="margin-bottom: var(--td-s-4);">
                            Transmit a continuous wave at ${(this.cwFreq / 1000000)} MHz, measure with a spectrum analyzer,
                            and enter the measured frequency below to calculate crystal accuracy.
                        </p>

                        ${this.data.radios === 2 ? html`
                            <div class="td-card-row" style="border-bottom: none; padding-bottom: 0;">
                                <span class="td-label">Radio</span>
                                <div class="td-segment" style="width: fit-content;">
                                    <button type="button" class="${this.radio === 1 ? 'is-active' : ''}" ?disabled=${this.started}
                                            @click="${() => { this.radio = 1 }}">Radio 1</button>
                                    <button type="button" class="${this.radio === 2 ? 'is-active' : ''}" ?disabled=${this.started}
                                            @click="${() => { this.radio = 2 }}">Radio 2</button>
                                </div>
                            </div>
                        ` : ''}

                        <!-- FEATURE:HAS_LR1121 -->
                        ${elrsState.settings.has_high_band && elrsState.settings.has_low_band ? html`
                            <div class="td-card-row" style="border-bottom: none; padding-bottom: 0;">
                                <span class="td-label">Set ${(this.data.center / 1000000)} MHz</span>
                                <span class="td-toggle ${this.setSubGHz ? 'is-on' : ''}"
                                      ?disabled=${this.started}
                                      @click="${this._toggleSubGHz}"></span>
                            </div>
                        ` : ''}
                        <!-- /FEATURE:HAS_LR1121 -->

                        <div style="padding: var(--td-s-4) 0 0;">
                            <button class="td-btn td-btn-primary" ?disabled=${this.started}
                                    @click="${this._startCW}">
                                Start Continuous Wave
                            </button>
                        </div>

                        <div style="margin-top: var(--td-s-4);">
                            <div class="td-field">
                                <label class="td-field-label">Measured Center Frequency</label>
                                <input class="td-input td-input-mono" id="measured" type="number" required
                                       placeholder="Enter measured peak / center frequency"
                                       @input="${this._measured}"
                                       @keypress="${(e) => {
                                           if (e.which !== 8 && e.which !== 0 && (e.which < 48 || e.which > 57))
                                               e.preventDefault();
                                       }}"/>
                            </div>
                        </div>

                        ${this.result.calculated ? html`
                            <div style="margin-top: var(--td-s-4);">
                                <div class="td-card" style="background: var(--td-bg-2);">
                                    <div class="td-card-row">
                                        <span class="td-label">Calculated XO Freq</span>
                                        <span class="td-mono">${this.result.calculated}</span>
                                    </div>
                                    <div class="td-card-row">
                                        <span class="td-label">XO Offset</span>
                                        <span class="td-mono">${this.result.offset} kHz</span>
                                    </div>
                                    <div class="td-card-row">
                                        <span class="td-label">XO Offset PPM</span>
                                        <span class="td-mono">${this.result.ppm} ppm</span>
                                    </div>
                                    <div class="td-card-row">
                                        <span class="td-label">Raw Offset</span>
                                        <span class="td-mono">${this.result.raw} kHz</span>
                                    </div>
                                    <div class="td-card-row" style="border-bottom: none;">
                                        <span class="td-label">Result</span>
                                        <span>${this.result.tldr}</span>
                                    </div>
                                </div>
                            </div>
                        ` : ''}
                    </div>
                </div>
            `
        else
            return html`
                <div class="td-h2" style="margin-bottom: var(--td-s-4);">Continuous Wave</div>
                <div class="td-card">
                    <div class="td-card-body td-mute td-small">${this._text}</div>
                </div>
            `
    }

    connectedCallback() {
        super.connectedCallback()
        const xmlhttp = new XMLHttpRequest()
        xmlhttp.onreadystatechange = () => {
            if (xmlhttp.readyState === 4 && xmlhttp.status === 200) {
                this._updateParams(JSON.parse(xmlhttp.responseText))
            } else {
                this._text = "Failed to load data."
            }
        }
        xmlhttp.open('GET', '/cw', true)
        xmlhttp.send()
    }

    _updateParams(data) {
        this.data = data
        this._updateFreq()
    }

    _toggleSubGHz() {
        if (this.started) return
        this.setSubGHz = !this.setSubGHz
        this._updateFreq()
    }

    _updateFreq() {
        this.cwFreq = this.data.center
        if (FEATURES.HAS_LR1121) {
            if (elrsState.settings?.has_high_band && elrsState.settings?.has_low_band) {
                if (!this.setSubGHz) {
                    this.cwFreq = this.data.center2
                }
            } else if (elrsState.settings?.has_high_band) {
                this.cwFreq = this.data.center2
            }
        }
        this._measured()
    }

    _startCW(e) {
        e.stopPropagation()
        e.preventDefault()
        this.started = true
        const formdata = new FormData()
        formdata.append('radio', this.radio)
        if (FEATURES.HAS_LR1121) {
            let subGHz = 0
            if (elrsState.settings.has_high_band && elrsState.settings.has_low_band) {
                subGHz = this.setSubGHz ? 1 : 0
            } else if (elrsState.settings.has_low_band) {
                subGHz = 1
            }
            formdata.append('subGHz', subGHz)
        }
        post('/cw', formdata)
    }

    _measured() {
        let xtalNominal = 52000000
        let warn_offset = 90000
        let bad_offset = 180000

        if (FEATURES.HAS_SX127X || FEATURES.HAS_LR1121) {
            xtalNominal = 32000000
            warn_offset = 100000
            bad_offset = 125000
        }

        if (!this.measured) return
        const calc = (this.measured.value / this.cwFreq) * xtalNominal
        const rawShift = Math.round(this.measured.value - this.cwFreq)

        let statusChip
        if (Math.abs(rawShift) < warn_offset) {
            statusChip = html`<span class="td-chip td-chip-ok">Good</span>`
        } else if (Math.abs(rawShift) < bad_offset) {
            statusChip = html`<span class="td-chip td-chip-warn">Marginal</span>`
        } else {
            statusChip = html`<span class="td-chip td-chip-bad">Out of range</span>`
        }

        this.result = {
            calculated: Math.round(calc),
            offset: Math.round(calc - xtalNominal) / 1000,
            ppm: Math.abs(Math.round(calc - xtalNominal)) / (xtalNominal / 1000000),
            raw: rawShift / 1000,
            tldr: statusChip
        }
    }
}
