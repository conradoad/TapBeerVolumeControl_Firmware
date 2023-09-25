const btnRelease = document.querySelector('#btn-release');
const inputReleaseVolume = document.querySelector('#input-release-volume');

btnRelease.addEventListener('click', () => {

    let volume = +inputReleaseVolume?.value

    if (volume == null || volume == 0){
        alert("Volume não pode ser vazio ou 0.");
        return;
    }

    fetch('/api/release_volume', {
        method: 'POST',
        headers: {
            'Content-Type': 'application/json'
        },
        body: JSON.stringify({
            volume: volume
        })
    })
    .catch(err => console.log(err));
});

const protocol = "ws";
const host = window.location.host;
const origin = window.location.origin;
const port = window.location.port;

const exampleSocket = new WebSocket(protocol + "://" + host + "/ws/volume");

exampleSocket.onopen = function (event) {
exampleSocket.send(
    "Aqui vai algum texto que o servidor esteja aguardando urgentemente!",
);};

exampleSocket.onmessage = function (event) {
    alert(event.data);
};