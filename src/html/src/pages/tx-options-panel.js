import {html, LitElement} from "lit"
import {customElement, state} from "lit/decorators.js"
import {elrsState, saveOptions} from "../utils/state.js"

import {postWithFeedback} from "../utils/feedback.js"

@customElement('tx-options-panel')
class TxOptionsPanel extends LitElement {
    @state() accessor domain
    @state() accessor isAirport
    @state() accessor baudRate
    @state() accessor tlmInterval
    @state() accessor fanRuntime

    createRenderRoot() {
        this.domain = elrsState.options.domain
        this.isAirport = elrsState.options['is-airport']
        this.baudRate = elrsState.options['airport-uart-baud']
        this.tlmInterval = elrsState.options['tlm-interval']
        this.fanRuntime = elrsState.options['fan-runtime']
        return this
    }

    render() {
        return html`
            <div class="td-row td-spread" style="margin-bottom: var(--td-s-4);">
                <span class="td-h2">Runtime Options</span>
                ${elrsState.options.customised ? html`<span class="td-chip td-chip-warn">Modified</span>` : ''}
            </div>
            <div class="td-card" style="margin-bottom: var(--td-s-4);">
                <div class="td-card-header">
                    <span class="td-h4">Settings</span>
                </div>

                <!-- FEATURE:HAS_SUBGHZ -->
                <div class="td-card-row">
                    <span class="td-label">Regulatory domain</span>
                    <select class="td-select" style="width: auto;"
                            @change="${(e) => this.domain = parseInt(e.target.value)}"
                            .value="${this.domain}">
                        ${['AU915', 'FCC915', 'EU868', 'IN866', 'AU433', 'EU433', 'US433', 'US433-Wide'].map((d, i) => html`
                            <option value="${i}" ?selected="${this.domain === i}">${d}</option>
                        `)}
                    </select>
                </div>
                <!-- /FEATURE:HAS_SUBGHZ -->

                <div class="td-card-row">
                    <span class="td-label">TLM report interval</span>
                    <div class="td-input-group" style="width: 120px;">
                        <input class="td-input td-input-mono" id="tlm" type="number" size="5"
                               @input="${(e) => this.tlmInterval = parseInt(e.target.value)}"
                               .value="${this.tlmInterval}"/>
                        <span class="td-btn" style="cursor:default; background: var(--td-bg-3);">ms</span>
                    </div>
                </div>

                <div class="td-card-row">
                    <span class="td-label">Fan runtime</span>
                    <div class="td-input-group" style="width: 120px;">
                        <input class="td-input td-input-mono" id="fan" type="number" size="3"
                               @input="${(e) => this.fanRuntime = parseInt(e.target.value)}"
                               .value="${this.fanRuntime}"/>
                        <span class="td-btn" style="cursor:default; background: var(--td-bg-3);">s</span>
                    </div>
                </div>

                <div class="td-card-row">
                    <span class="td-label">AirPort Serial device</span>
                    <span class="td-toggle ${this.isAirport ? 'is-on' : ''}"
                          @click="${() => { this.isAirport = !this.isAirport; this.requestUpdate() }}"></span>
                </div>

                ${this.isAirport ? html`
                    <div class="td-card-row">
                        <span class="td-label">AirPort UART baud</span>
                        <input class="td-input td-input-mono" id="baud" type="number" size="7"
                               @input="${(e) => this.baudRate = parseInt(e.target.value)}"
                               .value="${this.baudRate}"/>
                    </div>
                ` : ''}

                <div style="padding: var(--td-s-3) var(--td-s-4); border-top: 1px solid var(--td-line); display: flex; align-items: center; gap: var(--td-s-2);">
                    <div style="flex: 1;"></div>
                    ${elrsState.options.customised ? html`
                        <button class="td-btn td-btn-danger"
                                @click="${postWithFeedback('Reset Runtime Options', 'An error occurred resetting runtime options', '/reset?options', null)}">
                            Reset to defaults
                        </button>
                    ` : ''}
                    <button class="td-btn td-btn-primary"
                            ?disabled="${!this.checkChanged()}"
                            @click="${this.save}">Save</button>
                </div>
            </div>
        `
    }

    save(e) {
        e.preventDefault()
        const changes = {
            // FEATURE: HAS_SUBGHZ
            'domain': this.domain,
            // /FEATURE: HAS_SUBGHZ
            'tlm-interval': this.tlmInterval,
            'fan-runtime': this.fanRuntime,
            'is-airport': this.isAirport,
            'airport-uart-baud': this.baudRate
        }
        saveOptions(changes, () => { return this.requestUpdate() })
    }

    checkChanged() {
        let changed = false
        // FEATURE: HAS_SUBGHZ
        changed |= this.domain !== elrsState.options['domain']
        // /FEATURE: HAS_SUBGHZ
        changed |= this.tlmInterval !== elrsState.options['tlm-interval']
        changed |= this.fanRuntime !== elrsState.options['fan-runtime']
        changed |= this.isAirport !== elrsState.options['is-airport']
        changed |= this.baudRate !== elrsState.options['airport-uart-baud']
        return !!changed
    }
}
