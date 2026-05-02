import {html, LitElement} from "lit"
import {customElement, query, state} from "lit/decorators.js"
import {elrsState} from "../utils/state.js"
import {postWithFeedback} from "../utils/feedback.js"
import {autocomplete} from "../utils/autocomplete.js"

@customElement('wifi-panel')
class WifiPanel extends LitElement {

    @query('#sethome') accessor form
    @query('[name="network"]') accessor network
    @query('[name="password"]') accessor password

    @state() accessor selectedValue = '0'
    @state() accessor showLoader = true
    @state() accessor wifiOnInterval

    running = false

    constructor() {
        super()
        this._getNetworks = this._getNetworks.bind(this)
    }

    createRenderRoot() {
        this.wifiOnInterval = elrsState.options['wifi-on-interval'] === undefined ? 60 : elrsState.options['wifi-on-interval']
        return this
    }

    disconnectedCallback() {
        this.running = false
    }

    updated(_) {
        if (!this.running) this._getNetworks()
        this.running = true
    }

    render() {
        const isAP = elrsState.settings.mode !== 'STA'
        return html`
            <div class="td-row td-spread" style="margin-bottom: var(--td-s-4);">
                <span class="td-h2">WiFi Configuration</span>
                <span class="td-chip ${isAP ? 'td-chip-info' : 'td-chip-ok'}">
                    ${isAP ? 'Access Point' : 'STA: ' + elrsState.settings.ssid}
                </span>
            </div>

            <div class="td-card" style="margin-bottom: var(--td-s-4);">
                <div class="td-card-header">
                    <span class="td-h4">Network mode</span>
                </div>

                <form id="sethome">
                    <div class="td-card-body" style="padding: var(--td-s-3) var(--td-s-4); display: flex; flex-direction: column; gap: var(--td-s-2);">
                        ${[
                            ['0', 'Set new home network'],
                            ['1', 'Temporarily connect to a network, retain current home network setting'],
                            ['2', 'Temporarily enable "Access Point" mode, retain current home network setting'],
                            ['3', 'Forget home network setting, always use "Access Point" mode'],
                        ].map(([val, label]) => html`
                            <label style="display: flex; align-items: center; gap: var(--td-s-3); cursor: pointer; font-size: var(--td-fs-md); color: var(--td-fg-mute);">
                                <input type="radio" name="wifi-mode" value="${val}"
                                       .checked="${this.selectedValue === val}"
                                       @change="${() => { this.selectedValue = val }}"
                                       style="accent-color: var(--td-primary); width: 15px; height: 15px; flex-shrink: 0; cursor: pointer;"/>
                                ${label}
                            </label>
                        `)}
                    </div>

                    ${this.selectedValue === '0' || this.selectedValue === '3' ? html`
                        <div class="td-card-row">
                            <span class="td-label">WiFi auto-on interval</span>
                            <div class="td-input-group" style="width: 120px;">
                                <input class="td-input td-input-mono" id="interval" type="number" size="3"
                                       placeholder="Disabled"
                                       @input="${(e) => this.wifiOnInterval = parseInt(e.target.value)}"
                                       .value="${this.wifiOnInterval}"/>
                                <span class="td-btn" style="cursor:default; background: var(--td-bg-3);">s</span>
                            </div>
                        </div>
                    ` : ''}

                    ${this.selectedValue !== '2' && this.selectedValue !== '3' ? html`
                        <div class="td-card-row">
                            <span class="td-label">SSID</span>
                            <div class="autocomplete" style="position: relative; flex: 1;">
                                ${this.showLoader ? html`<span class="td-small td-mute" style="position:absolute;right:8px;top:50%;transform:translateY(-50%);">Scanning...</span>` : ''}
                                <input class="td-input" id="ssid" name="network" type="text"
                                       placeholder="SSID" autocomplete="off"
                                       @input="${() => this.requestUpdate()}"
                                       value="${elrsState.options['wifi-ssid']}"/>
                            </div>
                        </div>
                        <div class="td-card-row">
                            <span class="td-label">Password</span>
                            <input class="td-input" id="pwd" name="password" type="password" size="64"
                                   placeholder="Password" autocomplete="off"
                                   @input="${() => this.requestUpdate()}"
                                   value="${elrsState.options['wifi-password']}"/>
                        </div>
                    ` : ''}

                    <div style="padding: var(--td-s-3) var(--td-s-4); border-top: 1px solid var(--td-line); display: flex; align-items: center; gap: var(--td-s-2);">
                        <div style="flex: 1;"></div>
                        ${isAP && elrsState.options['wifi-ssid'] ? html`
                            <button class="td-btn td-btn-danger" type="button"
                                    @click="${postWithFeedback('Connect to Home Network', 'An error occurred connecting to the Home network', '/connect', null)}">
                                Connect to Home: ${elrsState.options['wifi-ssid']}
                            </button>
                        ` : ''}
                        <button class="td-btn td-btn-primary" type="button"
                                @click="${this._setupNetwork}"
                                ?disabled="${!(this.checkChanged() || this.selectedValue !== '0')}">Save</button>
                    </div>
                </form>
            </div>
        `
    }

    _setupNetwork(event) {
        event.preventDefault()
        const self = this
        switch (this.selectedValue) {
            case '0':
                postWithFeedback('Set Home Network', 'An error occurred setting the home network', '/sethome?save', function () {
                    return new FormData(self.form)
                }, function () {
                    elrsState.options = {
                        ...elrsState.options,
                        'wifi-ssid': self.network.value,
                        'wifi-password': self.password.value,
                        'wifi-on-interval': self.wifiOnInterval,
                        customised: true
                    }
                })(event)
                break
            case '1':
                postWithFeedback('Connect To Network', 'An error occurred connecting to the network', '/sethome', function () {
                    return new FormData(self.form)
                })(event)
                break
            case '2':
                postWithFeedback('Start Access Point', 'An error occurred starting the Access Point', '/access', null)(event)
                break
            case '3':
                postWithFeedback('Forget Home Network', 'An error occurred forgetting the home network', '/forget', function () {
                    return new FormData(self.form)
                }, function () {
                    elrsState.options = {
                        ...elrsState.options,
                        'wifi-on-interval': self.wifiOnInterval,
                        customised: true
                    }
                })(event)
                break
        }
    }

    _getNetworks() {
        const self = this
        const xmlhttp = new XMLHttpRequest()
        xmlhttp.onload = function () {
            if (self.running) {
                if (this.status === 204) {
                    setTimeout(self._getNetworks, 2000)
                } else {
                    const data = JSON.parse(this.responseText)
                    if (data.length > 0) {
                        self.showLoader = false
                        autocomplete(self.network, data)
                    }
                }
            }
        }
        xmlhttp.onerror = function () {
            if (self.running) setTimeout(self._getNetworks, 2000)
        }
        xmlhttp.open('GET', 'networks.json', true)
        xmlhttp.send()
    }

    checkChanged() {
        let changed = false
        changed |= this.wifiOnInterval !== elrsState.options['wifi-on-interval']
        changed |= this.network?.value !== elrsState.options['wifi-ssid']
        changed |= this.password?.value !== elrsState.options['wifi-password']
        return !!changed
    }
}
