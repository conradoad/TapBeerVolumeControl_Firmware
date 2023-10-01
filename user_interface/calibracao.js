const btnStart = document.querySelector('#btn-start-calibration');
const measuredVolumeSpan = document.querySelector('#span_measured_volume');
const realVolumeInput = document.querySelector('#input_real_volume');
const adjustFactorSpan = document.querySelector('#span-adjust-factor');
const btnFinish = document.querySelector('#btn-send-adjust');

realVolumeInput.addEventListener('input', (event) => {
    const measured = +measuredVolumeSpan.textContent;

    const adjustFactor = measured == 0 ? 1 : (event.target.value / measured);
    adjustFactorSpan.textContent = adjustFactor.toFixed(3);
});

btnStart.addEventListener('click', () => {
    calib_web_socket();
});

btnFinish.addEventListener('click', () => {

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

function calib_web_socket()
{
    const protocol = "ws";
    const host = window.location.host;

    const calibWebSocket = new WebSocket(protocol + "://" + host + "/ws/calib")
    calibWebSocket.onopen = function (event) {
        calibWebSocket.send("calib");
    };

    calibWebSocket.onmessage = function (event) {
        const resp = JSON.parse(event.data)

        if (resp.type == MSG_TYPE.VOLUME){
            const measured = parseFloat(resp.volume_consumed).toFixed(2);
            measuredVolumeSpan.textContent = measured;
            realVolumeInput.value = measured;
        }
    };
}
