const MSG_TYPE = {
    STATUS: 0,
    VOLUME: 1
}

const btnRelease = document.querySelector('#btn-release');
const inputReleaseVolume = document.querySelector('#input-release-volume');
const statusMessageSpan = document.querySelector('#span-status-message');
const consumedVolumeSpan = document.querySelector('#span-consumed-volume');
const balanceVolumeSpan = document.querySelector('#span-balance-volume');
const realVolumeInput = document.querySelector('#input_real_volume');
const adjustFactorSpan = document.querySelector('#span-adjust-factor');
const btnAdjust = document.querySelector('#btn-send-adjust');

btnRelease.addEventListener('click', () => {

    let volume = +inputReleaseVolume?.value

    if (volume == null || volume == 0){
        alert("Volume não pode ser vazio ou 0.");
        return;
    }

    statusMessageSpan.textContent = "Aguardando liberação"
    consumedVolumeSpan.textContent = parseFloat(0).toFixed(2);;
    balanceVolumeSpan.textContent = parseFloat(volume).toFixed(2);

    start_web_socket(volume);
});

realVolumeInput.addEventListener('input', (event) => {
    const measured = +consumedVolumeSpan.textContent;

    const adjustFactor = measured == 0 ? 1 : (event.target.value / measured);
    adjustFactorSpan.textContent = adjustFactor.toFixed(3);
});

btnAdjust.addEventListener('click', () => {

    let adjustFactor = +adjustFactorSpan.textContent

    if (adjustFactor == null || adjustFactor == 0){
        alert("Volume não pode ser vazio ou 0.");
        return;
    }

    fetch('/api/finish_calib', {
        method: 'POST',
        headers: {
            'Content-Type': 'application/json'
        },
        body: JSON.stringify({
            factor: adjustFactor
        })
    })
    .then(r => {
        measuredVolumeSpan.textContent = 0.00;
        realVolumeInput.value = 0.00;
        adjustFactorSpan.textContent = 1.00;
    })
    .catch(err => console.log(err));
});

function start_web_socket(volume)
{
    const protocol = "ws";
    const host = window.location.host;

    const exampleSocket = new WebSocket(protocol + "://" + host + "/ws/volume");

    exampleSocket.onopen = function (event) {
        exampleSocket.send(volume);
    };

    exampleSocket.onmessage = function (event) {
        const resp = JSON.parse(event.data)

        statusMessageSpan.textContent = resp.msg;
        consumedVolumeSpan.textContent = parseFloat(resp.volume_consumed).toFixed(2);
        balanceVolumeSpan.textContent = parseFloat(resp.volume_balance).toFixed(2);
    };
}