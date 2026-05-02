import './assets/td.js'
import './assets/td.css'
import './assets/td-extensions.css'
import {LitElement, html} from 'lit'
import {customElement, query, state} from "lit/decorators.js"
import {unsafeHTML} from 'lit/directives/unsafe-html.js'
import FEATURES from "./features.js"
import {elrsState, formatBand} from './utils/state.js'
import {cuteAlert} from "./utils/feedback.js";

import './pages/info-panel.js'

const tdIcon = name => unsafeHTML(TD.icon(name))

const ROUTE_LABELS = {
    info: 'Information', binding: 'Binding', options: 'Options',
    wifi: 'WiFi', update: 'Firmware Update', buttons: 'Buttons',
    models: 'Import/Export', connections: 'Connections', serial: 'Serial',
    hardware: 'Hardware Layout', cw: 'Continuous Wave', lr1121: 'LR1121 Firmware',
}

@customElement('titan-app')
export class App extends LitElement {
    @query("#sidedrawer") accessor sidedrawerEl
    @query("#main") accessor mainEl
    @query("#sidebar-backdrop") accessor backdropEl

    @state() accessor currentRoute = null

    constructor() {
        super()
        this.renderRoute = this.renderRoute.bind(this)
        this.showSidedrawer = this.showSidedrawer.bind(this)
        this.hideSidedrawer = this.hideSidedrawer.bind(this)
        this.toggleSidebar = this.toggleSidebar.bind(this)
    }

    createRenderRoot() {
        return this
    }

    render() {
        return html`
            <div id="sidedrawer">
                <div id="sidebar">
                    <div class="td-sidebar-header">
                        <div style="display: flex; align-items: center; gap: 10px;">
                            <svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 458 357" width="28" height="22" style="flex-shrink: 0;">
                                <path d="M0 0 C13.49749632 0 13.49749632 0 19.28686523 0.43969727 C19.92823111 0.48711626 20.56959698 0.53453526 21.23039818 0.58339119 C23.34012517 0.74031773 25.44932711 0.9034188 27.55859375 1.06640625 C29.10467699 1.18232263 30.65079538 1.297771 32.19694519 1.41279602 C35.52879163 1.66132087 38.86044046 1.91229475 42.19195557 2.16522217 C51.27946079 2.8546455 60.36827729 3.5264957 69.45703125 4.19921875 C70.8999416 4.3062122 70.8999416 4.3062122 72.37200165 4.41536713 C98.11685363 6.32277986 123.87080759 8.09901978 149.625 9.875 C151.91942446 10.03336102 154.21384789 10.19173701 156.50827026 10.35012817 C169.33869129 11.23568494 182.16926019 12.11907428 195 13 C194.39748885 16.73848664 193.32794461 19.71070852 191.62670898 23.0871582 C191.1320665 24.07621658 190.63742401 25.06527496 190.12779236 26.08430481 C189.58933182 27.14574203 189.05087128 28.20717926 188.49609375 29.30078125 C187.93938492 30.40995407 187.38267609 31.51912689 186.80909729 32.66191101 C185.32535526 35.61545016 183.83649634 38.56629126 182.34503174 41.51593018 C180.8580009 44.45968919 179.37756468 47.4067594 177.89648438 50.35351562 C175.62448484 54.87240688 173.3486727 59.38934426 171.06982422 63.90478516 C169.19688919 67.61832724 167.33137379 71.33524499 165.48095703 75.06005859 C165.11365982 75.79902878 164.74636261 76.53799896 164.36793518 77.29936218 C163.7181352 78.60886487 163.06995713 79.91917453 162.42393494 81.23054504 C160.75901812 84.58505755 158.97146366 87.81590161 157 91 C150.59034529 89.65350501 144.37760211 87.99286557 138.125 86.05078125 C137.18396927 85.76163849 136.24293854 85.47249573 135.27339172 85.17459106 C132.18127115 84.22374378 129.09066044 83.26808154 126 82.3125 C123.81025484 81.63838004 121.62039266 80.96464004 119.43041992 80.29125977 C114.88072263 78.89195818 110.33153244 77.49102576 105.78271484 76.08886719 C95.2968896 72.8578883 84.80459812 69.64803653 74.3125 66.4375 C72.29923224 65.82121679 70.28596726 65.20492447 68.27270508 64.58862305 C54.6067379 60.4058761 40.93878347 56.22964764 27.26766968 52.06375122 C25.22999179 51.4427229 23.19241285 50.82136979 21.15493774 50.19967651 C18.34708421 49.34305756 15.53883671 48.48774319 12.73046875 47.6328125 C11.90062469 47.37940598 11.07078064 47.12599945 10.21578979 46.86491394 C9.45130524 46.63250504 8.68682068 46.40009613 7.89916992 46.16064453 C7.23866135 45.95939972 6.57815277 45.75815491 5.89762878 45.55081177 C3.94549622 44.98417956 1.97201446 44.49300362 0 44 C0 29.48 0 14.96 0 0 Z " fill="#FFFFFF" transform="translate(29,34)"/>
                                <path d="M0 0 C0 14.52 0 29.04 0 44 C-7.195992 46.20556799 -14.39198997 48.41111635 -21.58803177 50.61652184 C-25.12164702 51.69948997 -28.65525425 52.78248422 -32.18884277 53.86553955 C-46.64175162 58.29537326 -61.0948185 62.72467109 -75.55078125 67.14453125 C-77.62158263 67.77783009 -79.69238342 68.41113083 -81.76318359 69.04443359 C-84.74801758 69.9572374 -87.73292848 70.86978576 -90.71807861 71.78155518 C-101.56321319 75.09411776 -112.40036396 78.42989317 -123.22265625 81.81640625 C-125.19366948 82.43185277 -127.16469783 83.04725083 -129.13574219 83.66259766 C-132.82983542 84.81606904 -136.52150333 85.97689805 -140.21191406 87.14208984 C-141.85603588 87.65619292 -143.50023694 88.17004265 -145.14453125 88.68359375 C-146.25792625 89.03859703 -146.25792625 89.03859703 -147.39381409 89.40077209 C-150.79667283 90.45713901 -153.39607328 91 -157 91 C-161.07500746 82.94752516 -165.08381867 74.87791874 -168.875 66.6875 C-172.12740945 59.68641384 -175.64967589 52.8547056 -179.27539062 46.04052734 C-183.00017532 39.03568688 -186.57648815 31.96349011 -190.08105469 24.84619141 C-190.74870436 23.50486122 -191.4280048 22.16931058 -192.11425781 20.83740234 C-192.48131836 20.12148926 -192.84837891 19.40557617 -193.2265625 18.66796875 C-193.73437256 17.68799927 -193.73437256 17.68799927 -194.25244141 16.68823242 C-195 15 -195 15 -195 13 C-193.72724213 12.91087845 -192.45448425 12.8217569 -191.14315796 12.72993469 C-178.81707992 11.8667678 -166.49103951 11.00306507 -154.16503048 10.13891315 C-147.61889874 9.67999576 -141.0727542 9.22126245 -134.52658081 8.76293945 C-97.65850321 6.18469498 -97.65850321 6.18469498 -60.79502106 3.54179955 C-58.2793666 3.35902149 -55.76368082 3.17668051 -53.24798584 2.99446106 C-46.54466903 2.50861913 -39.84147691 2.02143188 -33.13896847 1.52454853 C-29.88582591 1.28429649 -26.63233336 1.04886265 -23.37890625 0.8125 C-22.26637985 0.72852463 -21.15385345 0.64454926 -20.00761414 0.55802917 C-18.97070938 0.48344986 -17.93380463 0.40887054 -16.86547852 0.33203125 C-15.55257591 0.23498901 -15.55257591 0.23498901 -14.21315002 0.13598633 C-9.47945396 -0.06435226 -4.73793351 0 0 0 Z " fill="#FFFFFF" transform="translate(431,34)"/>
                                <path d="M0 0 C0.66 0 1.32 0 2 0 C7.18388008 9.59569291 12.1857622 19.26350089 17.02636719 29.03662109 C18.24415917 31.49235908 19.4695586 33.94422588 20.69726562 36.39501953 C21.32044446 37.64104921 21.94150876 38.88813848 22.56054688 40.13623047 C24.55637413 44.15201249 26.5888543 48.14658055 28.65390015 52.12728882 C29.95292524 54.63689912 31.24026742 57.15241866 32.52655029 59.6685791 C33.17059301 60.92161348 33.81965492 62.17208195 34.47393799 63.4197998 C35.40194224 65.19105038 36.31064717 66.97237046 37.21875 68.75390625 C37.75435547 69.78845947 38.28996094 70.8230127 38.84179688 71.88891602 C41.00492052 77.699347 39.91410137 83.31408469 38.75 89.27246094 C38.61636834 89.96615123 38.48273668 90.65984152 38.34505558 91.37455273 C37.90279991 93.66239141 37.45139951 95.94832446 37 98.234375 C36.68513721 99.8545013 36.37089524 101.47474835 36.05723572 103.09510803 C35.39114017 106.53015767 34.72118576 109.96442192 34.04806519 113.39810181 C32.99099853 118.79105821 31.94281099 124.18571403 30.89672852 129.58081055 C30.72397054 130.471706 30.55121256 131.36260145 30.37321949 132.28049374 C30.02654064 134.0683566 29.6798891 135.85622475 29.3332653 137.64409828 C28.81771516 140.30184609 28.30135361 142.95943516 27.7848053 145.61698914 C25.44292486 157.67143308 23.15412686 169.73435077 20.9375 181.8125 C17.39741667 201.04604366 13.72545417 220.25429439 10.04811478 239.46197128 C9.56465417 241.98874139 9.0820004 244.51566437 8.59957886 247.04263306 C7.83830781 251.02960478 7.07490624 255.01616273 6.3098259 259.00240517 C6.02421843 260.49207933 5.73929053 261.98188396 5.45506668 263.47182274 C5.06255436 265.52846672 4.66747849 267.58460194 4.27172852 269.640625 C4.04937027 270.80110352 3.82701202 271.96158203 3.59791565 273.15722656 C3 276 3 276 2 279 C1.34 279 0.68 279 0 279 C-0.17837402 278.00790527 -0.35674805 277.01581055 -0.54052734 275.99365234 C-5.72987958 247.20223597 -11.18114507 218.46273405 -16.69207764 189.73147583 C-17.678591 184.58803179 -18.66346793 179.44427608 -19.64777946 174.30041027 C-24.69261509 147.93142862 -24.69261509 147.93142862 -29.82266235 121.57893372 C-30.89778682 116.10459208 -31.96138694 110.62802657 -33.0244751 105.15133667 C-33.50405131 102.69646885 -33.98878557 100.24260247 -34.47894287 97.78982544 C-35.13913729 94.48588985 -35.7824927 91.17910878 -36.421875 87.87109375 C-36.61562714 86.9268454 -36.80937927 85.98259705 -37.00900269 85.00973511 C-37.88681346 80.37195464 -38.44986594 77.27209671 -36 73 C-35.45085937 71.86949219 -34.90171875 70.73898438 -34.3359375 69.57421875 C-33.68429466 68.27763722 -33.03053488 66.98211812 -32.375 65.6875 C-31.65359221 64.25393323 -30.93223957 62.82033871 -30.2109375 61.38671875 C-29.84323242 60.65759277 -29.47552734 59.9284668 -29.09667969 59.17724609 C-27.38990205 55.78876814 -25.69524345 52.39425876 -24 49 C-23.32949223 47.6588216 -22.65891923 46.3176758 -21.98828125 44.9765625 C-21.65779785 44.3155957 -21.32731445 43.65462891 -20.98681641 42.97363281 C-18.10546875 37.2109375 -18.10546875 37.2109375 -17.15063477 35.30151367 C-16.50893549 34.01795245 -15.86749634 32.73426115 -15.22631836 31.45043945 C-13.18588499 27.3660848 -11.13813421 23.28548958 -9.0859375 19.20703125 C-8.58553955 18.21074707 -8.0851416 17.21446289 -7.56958008 16.18798828 C-6.57884125 14.21549838 -5.58653643 12.24379413 -4.5925293 10.27294922 C-3.91444214 8.92160889 -3.91444214 8.92160889 -3.22265625 7.54296875 C-2.81797119 6.73883545 -2.41328613 5.93470215 -1.99633789 5.10620117 C-0.89742595 2.89197248 -0.89742595 2.89197248 0 0 Z " fill="#FFFFFF" transform="translate(229,52)"/>
                            </svg>
                            <span style="font-size: 15px; font-weight: 600; letter-spacing: -0.01em; color: var(--td-fg);">TitanLRS</span>
                        </div>
                    </div>
                    <nav id="sidebar-nav">
                        <div class="td-nav-section">Workspace</div>
                        <a id="menu-info" class="td-nav-item" href="#info">
                            ${tdIcon('info')} Information
                        </a>
                        <a id="menu-binding" class="td-nav-item" href="#binding">
                            ${tdIcon('bind')} Binding
                        </a>
                        <a id="menu-options" class="td-nav-item" href="#options">
                            ${tdIcon('sliders')} Options
                        </a>
                        <!-- FEATURE:IS_TX -->
                        ${elrsState.config['button-actions'] && elrsState.config['button-actions'].length !== 0 ? html`
                            <a id="menu-buttons" class="td-nav-item" href="#buttons">
                                ${tdIcon('button')} Buttons
                            </a>
                        ` : ''}
                        <a id="menu-models" class="td-nav-item" href="#models">
                            ${tdIcon('box')} Import/Export
                        </a>
                        <!-- /FEATURE:IS_TX -->
                        <!-- FEATURE:NOT IS_TX -->
                        ${elrsState.config.pwm !== undefined ? html`
                            <a id="menu-connections" class="td-nav-item" href="#connections">
                                ${tdIcon('link')} Connections
                            </a>
                        ` : ''}
                        <a id="menu-serial" class="td-nav-item" href="#serial">
                            ${tdIcon('serial')} Serial
                        </a>
                        <!-- /FEATURE:NOT IS_TX -->
                        <a id="menu-wifi" class="td-nav-item" href="#wifi">
                            ${tdIcon('wifi')} WiFi
                        </a>
                        <a id="menu-update" class="td-nav-item" href="#update">
                            ${tdIcon('flash')} Update
                        </a>

                        <div class="td-nav-section">Hardware</div>
                        <a id="menu-hardware" class="td-nav-item" href="#hardware">
                            ${tdIcon('cpu')} Hardware Layout
                        </a>

                        <div class="td-nav-section">System</div>
                        <a id="menu-cw" class="td-nav-item" href="#cw">
                            ${tdIcon('radio')} Continuous Wave
                        </a>
                        <!-- FEATURE:HAS_LR1121 -->
                        <a id="menu-lr1121" class="td-nav-item" href="#lr1121">
                            ${tdIcon('flash')} LR1121 Firmware
                        </a>
                        <!-- /FEATURE:HAS_LR1121 -->
                    </nav>
                </div>
            </div>

            <div id="topbar">
                <div class="td-row" style="width:100%; padding: 0 12px; gap: 8px; align-items: center;">
                    <button class="td-btn td-btn-icon td-btn-ghost td-mobile-only"
                            @click="${this.toggleSidebar}">
                        ${tdIcon('sidebar')}
                    </button>
                    <div style="display:flex; align-items:center; gap:8px; font-size:13px; color:var(--td-fg-mute);">
                        <span>Device</span>
                        <span style="color:var(--td-fg-faint);">/ </span>
                        <span>${elrsState.settings?.product_name || 'TitanLRS'}</span>
                        ${this.currentRoute ? html`
                            <span style="color:var(--td-fg-faint);">/ </span>
                            <span style="color:var(--td-fg);">${ROUTE_LABELS[this.currentRoute] || this.currentRoute}</span>
                        ` : ''}
                    </div>
                </div>
            </div>

            <div id="main-wrapper">
                <div id="main"></div>
            </div>

            <div id="sidebar-backdrop" @click="${this.hideSidedrawer}"></div>
        `
    }

    firstUpdated(_changedProperties) {
        window.addEventListener('hashchange', this.renderRoute)
        this.loadInitialData().catch(() => {}).then(() => {
            this.renderRoute()
        })
    }

    async loadInitialData() {
        try {
            const resp = await fetch('/config')
            if (!resp.ok) throw new Error('Failed to load config')
            const data = await resp.json()
            elrsState.settings = data.settings || {}
            elrsState.options = data.options || {}
            elrsState.config = data.config || {}
            document.title = 'TitanLRS ' + data.settings["module-type"] + ' WebUI'
            this.requestUpdate()
        } catch (e) {
            console.warn('Startup data load failed:', e)
        }
    }

    scrollMainToTop() {
        const doScroll = (behavior = 'smooth') => {
            try { window.scrollTo({top: 0, left: 0, behavior}) }
            catch { window.scrollTo(0, 0) }
        }
        requestAnimationFrame(() => requestAnimationFrame(() => doScroll('smooth')))
    }

    setActiveMenu(route) {
        const sidedrawer = this.sidedrawerEl || this.querySelector('#sidedrawer') || document.getElementById('sidedrawer')
        if (sidedrawer) {
            sidedrawer.querySelectorAll('a.td-nav-item').forEach(a => a.classList.remove('is-active'))
        }
        const id = 'menu-' + route
        const el = id ? (this.querySelector(`#${id}`) || document.getElementById(id)) : null
        if (el) el.classList.add('is-active')
    }

    buildRouteContent(route) {
        switch (route) {
            case 'info':        return '<info-panel></info-panel>'
            case 'binding':     return '<binding-panel></binding-panel>'
            case 'options':     return FEATURES.IS_TX ? '<tx-options-panel></tx-options-panel>' : '<rx-options-panel></rx-options-panel>'
            case 'wifi':        return '<wifi-panel></wifi-panel>'
            case 'update':      return '<update-panel></update-panel>'
            case 'connections': return !FEATURES.IS_TX && elrsState.config.pwm !== undefined ? '<connections-panel></connections-panel>' : ''
            case 'serial':      return !FEATURES.IS_TX ? '<serial-panel></serial-panel>' : ''
            case 'buttons':     return FEATURES.IS_TX ? '<buttons-panel></buttons-panel>' : ''
            case 'hardware':    return '<hardware-layout></hardware-layout>'
            case 'cw':          return '<continuous-wave></continuous-wave>'
            case 'models':      return '<models-panel></models-panel>'
            case 'lr1121':      return FEATURES.HAS_LR1121 ? '<lr1121-updater></lr1121-updater>' : ''
            default:            return ''
        }
    }

    generalGroupLoaded = false
    advancedGroupLoaded = false

    async loadGeneralGroup() {
        if (this.generalGroupLoaded) return
        try {
            const imports = [
                import('./pages/binding-panel.js'),
                import('./pages/wifi-panel.js'),
                import('./pages/update-panel.js')
            ]
            // FEATURE:IS_TX
            imports.push(import('./pages/tx-options-panel.js'))
            // FEATURE:NOT IS_8285
            imports.push(import('./pages/models-panel.js'))
            // /FEATURE:NOT IS_8285
            imports.push(import('./pages/buttons-panel.js'))
            // /FEATURE:IS_TX
            // FEATURE:NOT IS_TX
            imports.push(import('./pages/rx-options-panel.js'))
            imports.push(import('./pages/connections-panel.js'))
            imports.push(import('./pages/serial-panel.js'))
            // /FEATURE:NOT IS_TX
            await Promise.all(imports)
        } finally {
            this.generalGroupLoaded = true
        }
    }

    async loadAdvancedGroup() {
        if (this.advancedGroupLoaded) return
        try {
            const imports = [
                import('./pages/hardware-layout.js'),
                import('./pages/continuous-wave.js')
            ]
            // FEATURE:HAS_LR1121
            imports.push(import('./pages/lr1121-updater.js'))
            // /FEATURE:HAS_LR1121
            await Promise.all(imports)
        } finally {
            this.advancedGroupLoaded = true
        }
    }

    async ensureLoadedForRoute(route) {
        const generalRoutes = ['binding', 'options', 'wifi', 'update', 'connections', 'serial', 'buttons', 'models']
        const advancedRoutes = ['hardware', 'cw', 'lr1121']
        if (generalRoutes.includes(route)) {
            await this.loadGeneralGroup()
        } else if (advancedRoutes.includes(route)) {
            await this.loadAdvancedGroup()
        }
    }

    replaceMainWithTransition(newContent) {
        return new Promise(resolve => {
            const onEnd = () => {
                this.mainEl.removeEventListener('transitionend', onEnd)
                if (typeof newContent === 'string') {
                    this.mainEl.innerHTML = newContent
                } else if (newContent instanceof Node) {
                    this.mainEl.innerHTML = ''
                    this.mainEl.appendChild(newContent)
                } else {
                    this.mainEl.innerHTML = ''
                }
                this.mainEl.classList.add('route-fade-in')
                requestAnimationFrame(() => {
                    this.mainEl.classList.remove('route-fade-out')
                    requestAnimationFrame(() => {
                        this.mainEl.classList.remove('route-fade-in')
                        resolve()
                    })
                })
            }
            this.mainEl.addEventListener('transitionend', onEnd)
            this.mainEl.classList.add('route-fade-out')
            setTimeout(onEnd, 220)
        })
    }

    async renderRoute() {
        const route = (location.hash || '#info').replace('#', '')

        if (this.currentRoute && route === this.currentRoute) {
            this.setActiveMenu(route)
            return
        }

        const currentEl = this.mainEl?.firstElementChild
        if (currentEl && typeof currentEl.checkChanged === 'function') {
            try {
                let hasChanges = currentEl.checkChanged()
                let navigate = true
                if (hasChanges === true) {
                    navigate = (await cuteAlert({
                        type: 'question',
                        message: 'Do you wish to navigate away and discard changes to this page?',
                        title: 'Configuration Changed',
                        confirmText: 'Discard',
                        cancelText: 'Cancel'
                    })) === 'confirm'
                }
                if (navigate === false) {
                    if (this.currentRoute && this.currentRoute !== route) {
                        if (('#' + this.currentRoute) !== location.hash) {
                            location.hash = '#' + this.currentRoute
                        }
                        this.setActiveMenu(this.currentRoute)
                    }
                    return
                }
            } catch {
                // proceed with navigation
            }
        }

        await this.ensureLoadedForRoute(route)
        this.setActiveMenu(route)
        this.hideSidedrawer()
        const content = this.buildRouteContent(route)
        await this.replaceMainWithTransition(content)
        this.scrollMainToTop()
        this.currentRoute = route
    }

    showSidedrawer() {
        const sd = this.sidedrawerEl || document.getElementById('sidedrawer')
        const bd = this.backdropEl || document.getElementById('sidebar-backdrop')
        if (sd) sd.classList.add('active')
        if (bd) bd.classList.add('active')
    }

    hideSidedrawer() {
        const sd = this.sidedrawerEl || document.getElementById('sidedrawer')
        const bd = this.backdropEl || document.getElementById('sidebar-backdrop')
        if (sd) sd.classList.remove('active')
        if (bd) bd.classList.remove('active')
    }

    toggleSidebar() {
        const sd = this.sidedrawerEl || document.getElementById('sidedrawer')
        if (sd && sd.classList.contains('active')) {
            this.hideSidedrawer()
        } else {
            this.showSidedrawer()
        }
    }
}
