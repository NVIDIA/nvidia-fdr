# FDRTool Docker Container

This Docker container allows you to run FDRTool inside an isolated environment.

## Prerequisites

1. **Install Docker:**
  ``` Ensure that Docker is installed on your system. You can download it from [Docker's official website](https://www.docker.com/get-started).```

2. **Clone the Repository:**
 
  ```bash
  git clone ssh://git@gitlab-master.nvidia.com:12051/dgx/nvidia-fdr.git 
  ```
   

3. **Navigate to Docker Directory**

```bash 
cd nvidia-fdr/fdrtool/docker_container
```


4. **Locate Your Git Private Keys:**


```Before building the Docker image, find the path to your Git private keys.```

5. **Build and Run Docker Container**


```
bashsudo docker build --build-arg SSH_PRIVATE_KEY="$(cat /path/to/your/ssh-private-key)" -t attach_container_name_here .
```

```bash 
sudo docker run -it attach_container_name_here
```

Replace /path/to/your/ssh-private-key with the actual path, and attach_container_name_here with your desired container name.

6 . **Activate Virtual Environment and Use FDRTool**

Inside the container, run:
``` bash 
source env_fdr/bin/activate
```


7 . **Use fdrtool**
Now, you can use FDRTool within the activated virtual environment.
