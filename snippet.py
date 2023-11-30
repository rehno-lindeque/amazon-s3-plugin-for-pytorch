from torch.utils.data import DataLoader
from awsio.python.lib.io.s3.s3dataset import S3Dataset
from torchvision import transforms
from PIL import Image
import io

class S3ImageSet(S3Dataset):
    def __init__(self, url, transform=None):
        super().__init__(url)
        self.transform = transform
    def __getitem__(self, idx) :
        img_name, img = super(S3ImageSet, self).__getitem__(idx)
        img = Image.open(io.BytesIO(img)).convert('RGB')
        if self.transform is not None:
            img = self.transform(img)
        return img_name, img

preproc = transforms.Compose([
    transforms.ToTensor(),
    transforms.Normalize((0.485, 0.456, 0.406), (0.229, 0.224, 0.225)),
    transforms.Resize((100, 100))
])

url_list = [
    "s3://testimgs/tower01.jpg",
    "s3://testimgs/tower02.jpg"
]
dataset = S3ImageSet(url_list,transform=preproc)

dataloader = DataLoader(dataset,
        batch_size=1,
        num_workers=1)

for i, (name, img) in enumerate(dataloader):
    print(i, name, img.shape)
