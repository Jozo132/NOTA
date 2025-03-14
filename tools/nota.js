// #############################################################################################################################################
// 'nota.js' by J.Vovk
// #############################################################################################################################################
// This Node.JS script will transmit a binary image "Over The Air" to micro-controllers with OTA support.
//
// use it like: node nota -i <ESP_IP_address> -p <ESP_port> [-a password] -f <sketch.bin>
// Or to upload SPIFFS image:
// node nota -i <ESP_IP_address> -p <ESP_port> [-a password] -s -f <spiffs.bin>
//
// This script is based on the espota.py script from the ESP8266 Arduino library.
// The main difference between this script and the original espota.py is that now the OTA update process is fully done over the micro-controllers' TCP/IP socket.
// This means that the upload method can be used in cases with highly isolated industrial environments where the firewall rules are tight.
// There is also less command-line arguments to set, which makes this script less complicated to use.
//
// The downside is that this script is not compatible with the original OTA lib. (At least for now)
//
// #############################################################################################################################################
// Original espota.py by Ivan Grokhotkov:
// https://gist.github.com/igrr/d35ab8446922179dc58c
//
// Modified since 2015-09-18 from Pascal Gollor (https://github.com/pgollor)
// Modified since 2015-11-09 from Hristo Gochkov (https://github.com/me-no-dev)
// Modified since 2016-01-03 from Matthew O'Gorman (https://githumb.com/mogorman)
// #############################################################################################################################################
// @ts-check
(async () => {
    "use strict"

    /** @param { Error } e */
    const throw_error = e => {
        console.error('    ' + e.message);
        process.exit(1)
    }
    process.on('uncaughtException', throw_error)
    process.on('unhandledRejection', throw_error)

    const net = require('net')
    const fs = require('fs')
    const crypto = require('crypto')
    const md5 = data => crypto.createHash('md5').update(typeof data === 'string' ? Buffer.from(data) : data).digest("hex")
    const delay = ms => new Promise(r => setTimeout(r, ms))
    const timestamp = used => !used ? '' : `[${new Date().toISOString().replace(/T/, ' ').replace(/\..+/, '')}]: `
    const print = (...args) => process.stdout.write(args.filter(x => x !== undefined).join(' '))
    const println = (...args) => print(...args, '\r\n')



    // const CHUNK_SIZE = 1460 // espota.py default: 1460 bytes
    // const CHUNK_SIZE = 1460 * 4 // nota.js: tested with ESP8266 and seems to be fast and reliable at 4x the size of the original espota.py
    const CHUNK_SIZE = 2048 // nota.js: tested with STM32F4 using W5500
    const DEFAULT_PORT = 8266

    // Commands
    const FLASH = 0
    const SPIFFS = 100
    const AUTH = 200
    const total_bars = 40

    // Parse command line arguments
    const argv_raw = process.argv.slice(2)
    const argv = {}
    for (let i = 0; i < argv_raw.length; i++) {
        if (argv_raw[i]) {
            if (argv_raw[i].startsWith('--')) {
                const e_index = argv_raw[i].indexOf('=')
                const [key, value] = e_index > 0 ? [argv_raw[i].substring(0, e_index), argv_raw[i].substring(e_index + 1)] : [argv_raw[i], true]
                argv[key.substring(2)] = value
            } else if (argv_raw[i] && argv_raw[i].startsWith('-') && (argv_raw[i + 1] || '').startsWith('-')) {
                const key = argv_raw[i].substring(1)
                argv[key] = true
            } else if (argv_raw[i] && argv_raw[i].startsWith('-')) {
                const key = argv_raw[i].substring(1)
                argv[key] = argv_raw[++i]
            }
        }
    }

    const host = argv.i || argv.ip || ''
    const port = argv.p || argv.port || DEFAULT_PORT
    const auth = argv.a || argv.auth || ''
    const image = argv.f || argv.file || ''
    const command = (argv.s || argv.spiffs || false) ? SPIFFS : FLASH
    const debug = !!(argv.d || argv.debug || false)
    const ts = !!(argv.t || argv.timestamp || false)

    if (!host) throw new Error('Missing parameter [-i] / [--ip] for the target IP address.')
    if (!image) throw new Error('Missing parameter [-f] / [--file] for the binary image file.')
    if (!fs.existsSync(image)) throw new Error(`File ${JSON.stringify(image)} does not exist.`)

    /** 
     * @param { { host: String, port: Number, debug?: Boolean} } remote_address
     * @returns { Promise<{ 
     *      socket: net.Socket, 
     *      setTimeout: (ms: Number) => void, 
     *      write: (data: Buffer | String) => Promise,
     *      read: (size?: Number) => String,
     *      readUntil: (delimiter: String) => String,
     *      peek: (size?: Number) => String,
     *      peekAll: () => String,
     *      readAll: () => String,
     *      close: () => void,
     *      end: () => void,
     *      doAwait: (size?: Number) => Promise,
     *      available: () => Number,
     * }> }
    */
    const socket_connect = ({ host, port, debug }) => new Promise((resolve, reject) => {
        const c = new net.Socket()
        let data = ''
        const connection = {
            socket: c,
            setTimeout: ms => c.setTimeout(ms),
            write: data => new Promise((res, rej) => {
                c.write(typeof data === 'string' ? Buffer.from(data) : data, e => e ? rej(e) : res(1))
                if (debug) println(`${timestamp(ts)}> TO ${host}:${port} ${JSON.stringify(data + '')}`)
            }),
            read: (size = 1) => {
                const output = data.substring(0, size)
                data = data.substring(size)
                return output
            },
            readUntil: delimiter => {
                const index = data.indexOf(delimiter)
                if (index === -1) {
                    const output = data
                    data = ''
                    return output
                }
                const output = data.substring(0, index + delimiter.length)
                data = data.substring(index + delimiter.length)
                return output
            },
            peek: (size = 1) => data.substring(0, size),
            peekAll: () => data,
            readAll: () => {
                const output = data
                data = ''
                return output
            },
            close: () => c.destroy(),
            end: () => c.destroy(),
            doAwait: async (size = 1) => {
                let done = false
                let cnt = 1500
                while (!done) {
                    await delay(1)
                    if (cnt-- <= 0) throw new Error(`Timeout while waiting for ${host}:${port} to receive ${size} bytes of data`)
                    if (data.length >= size) done = true
                }
            },
            available: () => data.length,
        }
        c.on('connect', () => {
            resolve(connection)
            if (debug) println(`${timestamp(ts)}Connecting to ${host}:${port} SUCCESS`)
        })
        c.on('error', reject)
        c.on('timeout', reject)
        c.on('data', d => {
            const new_data = d.toString()
            if (debug) println(`${timestamp(ts)}< FROM ${host}:${port} ${JSON.stringify(new_data)}`)
            data += new_data
        })
        c.connect(port, host)
    })

    const connect = async (message) => {
        let sock = undefined
        let con_retries = 0, connected = false
        for (; con_retries < 10 && !connected;) {
            if (con_retries === 1) { print(`${timestamp(ts)}Retrying...`) }
            if (con_retries > 1) print('.')
            try {
                sock = await socket_connect({ host, port, debug }) // Create a TCP/IP client socket connection
            } catch (e) { throw new Error(e.message) }
            try {
                sock.setTimeout(100)
                await sock.write(message)
                await sock.doAwait()
                connected = true
            } catch (e) {
                try { sock ? sock.end() : '' } catch (e) { }
                await delay(100)
                con_retries++
            }
        }
        if (con_retries > 0) println(connected ? ' connected!' : ' failed!')
        if (!sock) throw new Error(`Failed to connect to ${host}:${port}`)
        return sock
    }

    const verify = sock => new Promise(async (resolve, reject) => {
        try {
            print(`${timestamp(ts)}Verifying...`)
            sock.setTimeout(10)
            await delay(500)
            let received_ok = false
            let nack = 0
            let cnt = 0
            while (!received_ok) {
                if (cnt++ % 10 === 0) print('.')
                await delay(100)
                nack = sock.available() ? 0 : nack + 1
                if (nack >= 100) {
                    println(' failed!')
                    throw new Error(`No response from target`)
                }
                while (sock.available() && +sock.peek() >= 0 && +sock.peek() <= 9) sock.read()
                if (sock.available()) received_ok = true
            }
            await delay(10)
            resolve(1)
        } catch (e) { reject(e) }
        println()
    })

    try {
        const time_start = +new Date
        let filename = image
        if (fs.existsSync(filename + '.signed')) { // Check whether Signed Update is used.
            filename = filename + '.signed'
            println(`${timestamp(ts)}Detected Signed Update. "${filename}" will be uploaded instead.`)
        }
        if (filename.endsWith('.elf')){
            // Convert ELF to BIN
            const { exec } = require('child_process')
            const exec_promise = (cmd) => new Promise((resolve, reject) => exec(cmd, (err, stdout, stderr) => err ? reject(err) : resolve(stdout)))
            
            const binfile = filename.replace('.elf', '.bin')
            // arm-none-eabi-objcopy -O binary firmware.elf firmware.bin
            const cmd = `arm-none-eabi-objcopy -O binary ${filename} ${binfile}`
            println(`${timestamp(ts)}Converting ELF to BIN`)
            await exec_promise(cmd)
            filename = binfile
        }
        const file_content = fs.readFileSync(filename, { encoding: null})
        const content_size = file_content.length
        const file_md5 = await md5(file_content)
        println(`${timestamp(ts)}Sending OTA ${command === SPIFFS ? 'SPIFFS' : 'Flash'} update request to ${host}:${port}`)
        const message = `${command} ${content_size} ${file_md5}\n`
        const sock = await connect(message)
        const res_update = sock.readAll()
        if (!res_update) throw new Error(`No Answer from ${host}:${port}`)
        if (res_update.startsWith('AUTH')) {
            if (!auth || auth === true) throw new Error(`Target requires authentication. Please provide the password with [-a] / [--auth]`)
            const nonce = res_update.split(' ')[1]
            const cnonce_text = `${filename}${content_size}${file_md5}${host}`
            const cnonce = md5(cnonce_text)
            const passmd5 = md5(auth)
            const challenge_text = `${passmd5}:${nonce}:${cnonce}`
            const challenge = md5(challenge_text)
            print(`${timestamp(ts)}Authenticating...`)
            await sock.write(`${AUTH} ${cnonce} ${challenge}\n`)
            await sock.doAwait()
            const res_auth = sock.readAll()
            if (!res_auth) {
                println(' failed!')
                throw new Error('No Answer to our Authentication')
            }
            if (res_auth != 'OK') {
                println(' failed!')
                throw new Error(res_auth)
            }
            println(' done!')
        } else if (res_update !== 'OK') {
            throw new Error(`Bad invitation response: ${JSON.stringify(res_update)}`)
        } else {
            await delay(200)
            sock.readAll() // OK, we're good to go. Clean up the socket and start the update.
        }
        const upload_start = +new Date
        let offset = 0
        let done = false
        println(`${timestamp(ts)}Uploading ${[...file_content.subarray(0, 16)].map(x => x.toString(16).toLocaleUpperCase().padStart(2, '0')).join(' ')}...`)
        println(`${timestamp(ts)}Total:    |<${'-'.repeat(total_bars - 2)}>| ${content_size} bytes`)

        print(`${timestamp(ts)}Progress: [`)
        // split file into chunks of size CHUNK_SIZE
        for (let i = 0, c = 0; i < content_size && !done; i += CHUNK_SIZE) {
            // Without using the deprecated Buffer.prototype.slice method
            const size = (Math.min(i + CHUNK_SIZE, content_size) - i)
            const chunk = file_content.subarray(i, i + CHUNK_SIZE)
            await sock.write(chunk)
            await sock.doAwait()
            const response = sock.readAll()
            if (response) {
                if (response.includes('OK')) done = true
                else {
                    const expected = `${size}`
                    if (expected !== response) throw new Error(`Bad response: ${JSON.stringify(response)} (expected: ${JSON.stringify(expected)})`)
                }
            }
            offset += chunk.length
            const progress = offset / content_size
            const p = Math.floor(progress * total_bars)
            if (p !== c) {
                c = p
                print('=')
            }
        }
        const upload_duration = ((+new Date - upload_start) / 1000).toFixed(2)
        println(`] ${upload_duration} seconds`)
        await verify(sock)
        const reply = sock.readAll()
        if (!reply.includes('OK')) throw new Error(`Problem while uploading: ${JSON.stringify(reply)}`)
        println(`${timestamp(ts)}OTA update finished in ${((+new Date - time_start) / 1000).toFixed(2)} seconds.`)
        sock.end()
    } catch (e) { throw_error(e) }
    process.exit(0)
})()