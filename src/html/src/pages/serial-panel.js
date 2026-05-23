import {html, LitElement} from "lit"
import {customElement, state} from "lit/decorators.js"
import {_renderOptions} from "../utils/libs.js"
import {elrsState, saveOptionsAndConfig} from "../utils/state.js"
import {PWM_MODE_SERIAL, PWM_MODE_SERIAL2RX, PWM_MODE_SERIAL2TX} from "./connections-panel.js";
import {SERIAL_OPTIONS1, SERIAL_OPTIONS2} from "../utils/globals.js";

@customElement('serial-panel')
class SerialPanel extends LitElement {

    PROTOCOL_AIRPORT = SERIAL_OPTIONS1.length - 1

    @state() accessor serial1Protocol
    @state() accessor serial2Protocol
    @state() accessor baudRate
    @state() accessor sbusFailsafe
    @state() accessor isAirport
    @state() accessor djiArmed

    createRenderRoot() {
        this.isAirport = elrsState.options['is-airport']
        this.serial1Protocol = this.isAirport ? this.PROTOCOL_AIRPORT : elrsState.config['serial-protocol']
        this.serial2Protocol = elrsState.config['serial1-protocol']
        this.baudRate = elrsState.options['rcvr-uart-baud']
        this.sbusFailsafe = elrsState.config['sbus-failsafe']
        this.djiArmed = elrsState.options['dji-permanently-armed']
        this._saveSerial = this._saveSerial.bind(this)
        return this
    }

    render() {
        return html`
            <div class="td-h2" style="margin-bottom: var(--td-s-4);">Serial / UART Options</div>

            ${this._hasSerial1() || this._hasSerial2() ? html`
                <div class="td-card">
                    <div class="td-card-header">
                        <span class="td-h4">Protocol</span>
                    </div>

                    ${this._hasSerial1() ? html`
                        <div class="td-card-row">
                            <span class="td-label">Serial 1 Protocol</span>
                            <select class="td-select" name="serial-protocol" @change=${this._updateSerial1}>
                                ${_renderOptions(SERIAL_OPTIONS1, this.serial1Protocol)}
                            </select>
                        </div>
                    ` : ''}

                    ${this._hasSerial2() ? html`
                        <div class="td-card-row">
                            <span class="td-label">Serial 2 Protocol</span>
                            <select class="td-select" name="serial1-protocol" @change=${this._updateSerial2}>
                                ${_renderOptions(SERIAL_OPTIONS2, this.serial2Protocol)}
                            </select>
                        </div>
                    ` : ''}

                    ${this._displayBaudRate() ? html`
                        <div class="td-card-row">
                            <span class="td-label">CRSF / Airport baud</span>
                            <div class="td-input-group" style="width: 140px;">
                                <input class="td-input td-input-mono" type="number" size="7"
                                       @input=${(e) => this.baudRate = parseInt(e.target.value)}
                                       .value="${this.baudRate}"/>
                                <span class="td-btn" style="cursor:default; background: var(--td-bg-3);">baud</span>
                            </div>
                        </div>
                    ` : ''}

                    ${this._sbusSelected() ? html`
                        <div class="td-card-row">
                            <span class="td-label">SBUS Failsafe</span>
                            <select class="td-select" name="serial-failsafe" style="width: 160px;">
                                <option value="0">No Pulses</option>
                                <option value="1">Last Position</option>
                            </select>
                        </div>
                    ` : ''}

                    ${this._displayPortSelected() ? html`
                        <div class="td-card-row">
                            <span class="td-label">Permanently arm DJI</span>
                            <span class="td-toggle ${this.djiArmed ? 'is-on' : ''}"
                                  @click="${() => { this.djiArmed = !this.djiArmed; this.requestUpdate() }}"></span>
                        </div>
                    ` : ''}

                    <div style="padding: var(--td-s-3) var(--td-s-4); border-top: 1px solid var(--td-line); display: flex; align-items: center;">
                        <div style="flex: 1;"></div>
                        <button class="td-btn td-btn-primary"
                                ?disabled="${!this.checkChanged()}"
                                @click="${this._saveSerial}">Save</button>
                    </div>
                </div>
            ` : html`
                <div class="td-card">
                    <div class="td-card-body">
                        <span class="td-chip td-chip-info" style="margin-right: 8px;">PWM Receiver</span>
                        <span class="td-body td-mute">No serial pins configured. Go to
                            <a href="#connections" style="color: var(--td-brand);">Connections</a> to enable serial IO pins.
                        </span>
                    </div>
                </div>
            `}
        `
    }

    _hasSerial1() {
        if (!elrsState.config['pwm']) return true
        for (const pwm of elrsState.config.pwm) {
            const mode = (pwm.config >> 16) & 0xF
            if (mode === PWM_MODE_SERIAL) return true
        }
        for (const pwm of elrsState.config.pwm) {
            if (pwm.features & 3 !== 0) return false
        }
        return !!elrsState.settings.has_serial_pins
    }

    _hasSerial2() {
        if (!elrsState.config['pwm']) {
            return elrsState.config['serial1-protocol'] !== undefined
        }
        for (const pwm of elrsState.config.pwm) {
            const mode = (pwm.config >> 16) & 15
            if (mode === PWM_MODE_SERIAL2RX || mode === PWM_MODE_SERIAL2TX) return true
        }
        return false
    }

    _updateSerial1(e) {
        this.serial1Protocol = parseInt(e.target.value)
        this.isAirport = this.serial1Protocol === this.PROTOCOL_AIRPORT
        if (this.serial1Protocol === 0 || this.serial1Protocol === 1) {
            this.baudRate = 420000
            this.requestUpdate()
        }
    }

    _updateSerial2(e) {
        this.serial2Protocol = parseInt(e.target.value)
    }

    _displayBaudRate() {
        return this.isAirport || this.serial1Protocol === 0 || this.serial1Protocol === 1 ||
               this.serial2Protocol === 1 || this.serial2Protocol === 2
    }

    _sbusSelected() {
        return this.serial1Protocol === 2 || this.serial1Protocol === 3 ||
               this.serial2Protocol === 3 || this.serial2Protocol === 4
    }

    _displayPortSelected() {
        return this.serial1Protocol === 8 || this.serial2Protocol === 9
    }

    _configChanged() {
        return (!this.isAirport && this.serial1Protocol !== elrsState.config['serial-protocol']) ||
            this.serial2Protocol !== elrsState.config['serial1-protocol'] ||
            this.sbusFailsafe !== elrsState.config['sbus-failsafe']
    }

    _optionsChanged() {
        return this.isAirport !== elrsState.options['is-airport'] ||
            this.baudRate !== elrsState.options['rcvr-uart-baud'] ||
            this.djiArmed !== elrsState.options['dji-permanently-armed']
    }

    checkChanged() {
        return this._configChanged() || this._optionsChanged()
    }

    _saveSerial(e) {
        e.preventDefault()
        saveOptionsAndConfig({
            options: {
                'is-airport': this.isAirport,
                'rcvr-uart-baud': this.baudRate,
                'dji-permanently-armed': this.djiArmed,
            },
            config: {
                'serial-protocol': this.isAirport ? 0 : this.serial1Protocol,
                'serial1-protocol': this.serial2Protocol,
                'sbus-failsafe': this.sbusFailsafe
            }
        }, () => { this.requestUpdate() })
    }
}
