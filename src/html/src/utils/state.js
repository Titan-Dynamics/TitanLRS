import {State} from "@lit-app/state";
import {errorAlert, saveWithReboot} from "./feedback.js";
import {transport} from "./transport.js";

class ElrsState extends State {
    config = {}
    options = {}
    settings = {}
}

export function formatBand() {
    if (elrsState.settings) {
        if (elrsState.settings.reg_domain_low && elrsState.settings.reg_domain_high) {
            return elrsState.settings.reg_domain_low + '/' + elrsState.settings.reg_domain_high
        }
        if (elrsState.settings.reg_domain_low)
            return elrsState.settings.reg_domain_low
        return elrsState.settings.reg_domain_high
    }
}

export function saveConfig(changes, successCB) {
    const currentPWM = elrsState.config.pwm
    if (changes.pwm) {
        // update pwm settings
        for (let i = 0; i < currentPWM.length; i++) {
            currentPWM[i].config = changes.pwm[i]
        }
    } else if (currentPWM) {
        // preserve original pwm settings
        changes.pwm = []
        for (let i = 0; i < currentPWM.length; i++) {
            changes.pwm.push(currentPWM[i].config)
        }
    }
    const newConfig = {...elrsState.config, ...changes}
    return saveWithReboot('Configuration Update Succeeded', 'Configuration Update Failed',
        (cfg) => transport.saveConfig(cfg), newConfig, () => {
            elrsState.config = {...newConfig, pwm: currentPWM}
            if (successCB) successCB()
        })
}

export function saveOptions(changes, successCB) {
    const newOptions = {...elrsState.options, ...changes, customised: true}
    return saveWithReboot('Configuration Update Succeeded', 'Configuration Update Failed',
        (opts) => transport.saveOptions(opts), newOptions, () => {
            elrsState.options = newOptions
            if (successCB) successCB()
        })
}

export function saveOptionsAndConfig(changes, successCB) {
    const newOptions = {...elrsState.options, ...changes.options, customised: true}
    return Promise.resolve()
        .then(() => transport.saveOptions(newOptions))
        .then(() => saveConfig(changes.config, () => {
            elrsState.options = newOptions
            if (successCB) successCB()
        }))
        .catch(async (err) => {
            await errorAlert('Configuration Update Failed', (err && err.message) || 'Request failed')
        })
}

export let elrsState = new ElrsState()
