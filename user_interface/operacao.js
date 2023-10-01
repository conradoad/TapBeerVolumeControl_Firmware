const MSG_TYPE = {
    STATUS: 0,
    VOLUME: 1
}

const btnRelease = document.querySelector('#btn-release');
const inputReleaseVolume = document.querySelector('#input-release-volume');
const statusMessageSpan = document.querySelector('#span-status-message');
const consumedVolumeSpan = document.querySelector('#span-consumed-volume');
const balanceVolumeSpan = document.querySelector('#span-balance-volume');

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

        if (resp.type == MSG_TYPE.STATUS) {
            statusMessageSpan.textContent = resp.msg;
        }
        else if (resp.type == MSG_TYPE.VOLUME){
            consumedVolumeSpan.textContent = parseFloat(resp.volume_consumed).toFixed(2);
            balanceVolumeSpan.textContent = parseFloat(resp.volume_balance).toFixed(2);
        }
    };
}